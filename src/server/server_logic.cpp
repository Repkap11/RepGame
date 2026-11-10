
#include "server/server_logic.hpp"
#include "server/server.hpp"
#include "common/chunk.hpp"
#include "common/light.hpp"
#include "common/net/packet.hpp"

#include <climits>
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerLogic::init( const char *world_name ) {
    this->map_storage.init( world_name );
    this->entities.init( &this->map_storage );
    this->containers.init( this );
    int64_t saved_world_time = 0;
    if ( this->map_storage.read_world_time( saved_world_time ) ) {
        this->world_time_base = saved_world_time;
    } else {
        // Fresh world starts at noon, same default as the client.
        this->world_time_base = DAY_LENGTH_TICKS / 4;
    }
}

int64_t ServerLogic::worldTimeNow( ) const {
    if ( !this->world_time_running ) {
        return this->world_time_base;
    }
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ) - this->world_time_anchor )
                                .count( );
    return this->world_time_base + static_cast<int64_t>( ( elapsed_us * UPS_RATE ) / 1000000 );
}

void ServerLogic::sendWorldTime( Server &server, int client_fd ) {
    NetWorldTimePayload p{ this->worldTimeNow( ) };
    PacketWriter w;
    net_serialize_world_time( w, p );
    server.queue_message( client_fd, NetMsgType::WORLD_TIME, 0, w.take_buf( ) );
}

void ServerLogic::broadcastWorldTime( Server &server ) {
    NetWorldTimePayload p{ this->worldTimeNow( ) };
    PacketWriter w;
    net_serialize_world_time( w, p );
    server.broadcast( NetMsgType::WORLD_TIME, 0, w.buf( ) );
}

void ServerLogic::on_client_connected( Server &server, int client_fd ) {
    pr_debug( "%d", client_fd );
    this->entities.on_client_connected( server, client_fd );

    // First client of a session starts the clock (it freezes again when the
    // last one leaves); everyone gets told the current time right away.
    if ( !this->world_time_running ) {
        this->world_time_running = true;
        this->world_time_anchor = std::chrono::steady_clock::now( );
        this->next_time_broadcast = this->world_time_anchor + std::chrono::seconds( WORLD_TIME_SYNC_SECONDS );
    }
    this->sendWorldTime( server, client_fd );

    // Tell the other clients about this new client
    server.broadcast_empty( NetMsgType::PLAYER_CONNECTED, client_fd, client_fd );

    // Tell this client about the other clients already online
    for ( int online_id = 0; online_id < MAX_CLIENT_FDS; online_id++ ) {
        if ( online_id == client_fd ) {
            continue;
        }
        PacketType_DataPlayer *playerData = server.get_data_if_client_connected( online_id );
        if ( playerData != NULL ) {
            NetPlayerPayload p;
            p.x = playerData->x;
            p.y = playerData->y;
            p.z = playerData->z;
            memcpy( p.rotation, playerData->rotation, sizeof( p.rotation ) );
            p.eye_height = playerData->eye_height;
            p.flags = playerData->flags;
            p.health = playerData->health;
            p.held_item = playerData->held_item;
            PacketWriter w;
            net_serialize_player( w, p );
            server.queue_message( client_fd, NetMsgType::CLIENT_INIT, online_id, w.take_buf( ) );
            pr_debug( "Notifying new player:%d of existing player:%d", client_fd, online_id );
        }
    }
}

void ServerLogic::cancelChunkSave( const glm::ivec3 &chunk_pos ) {
    // Lazy cancel: disarming the entry's deadline orphans its wake_schedule
    // slot, which processTimers skips when it pops.
    const auto it = this->world_cache.find( chunk_pos );
    if ( it != this->world_cache.end( ) ) {
        it->second.wake_deadline = std::chrono::steady_clock::time_point::max( );
    }
}

void ServerLogic::scheduleChunkSave( const glm::ivec3 &chunk_pos ) {
    // Cancel any pending timer for this chunk, then schedule a new one.
    // A container-only arm creates a timer-only cache entry (empty blocks)
    // to carry the deadline; block readers treat it as a miss.
    this->cancelChunkSave( chunk_pos );
    ChunkCacheEntry &entry = this->world_cache[ chunk_pos ];
    const auto wake = std::chrono::steady_clock::now( ) + std::chrono::seconds( CHUNK_IDLE_SAVE_SECONDS );
    entry.wake_deadline = wake;
    this->wake_schedule.insert( { wake, chunk_pos } );
}

void ServerLogic::record_block( const glm::ivec3 &block_pos, BlockState &block_state ) {
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
    glm::ivec3 diff = block_pos - ( chunk_pos * CHUNK_SIZE_I );

    for ( int i = -1; i < 2; i++ ) {
        const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
        if ( !needs_update_x ) {
            continue;
        }
        for ( int j = -1; j < 2; j++ ) {
            const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
            if ( !needs_update_y ) {
                continue;
            }
            for ( int k = -1; k < 2; k++ ) {
                const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                if ( !needs_update_z ) {
                    continue;
                }

                glm::ivec3 new_chunk_pos = chunk_pos + glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
                glm::ivec3 new_diff = block_pos - ( new_chunk_pos * CHUNK_SIZE_I );
                int new_block_index = Chunk::get_index_from_coords( new_diff );

                // pr_debug( "Updating chunk: %d %d %d  %d", new_chunk_pos.x, new_chunk_pos.y, new_chunk_pos.z, new_block_index );
                auto it_cache = this->world_cache.find( new_chunk_pos );
                // A chunk missing from the cache was idle-saved and evicted
                // (or never touched): merge its on-disk diff in first, else
                // this edit alone would overwrite the file on the next
                // persist and lose every earlier edit. A timer-only entry
                // (empty blocks, armed by a container edit) hasn't merged
                // either — route it through loadIntoCache too.
                ChunkCacheEntry &entry = *( it_cache != this->world_cache.end( ) && !it_cache->second.blocks.empty( )
                                                ? &it_cache->second
                                                : this->loadIntoCache( new_chunk_pos ) );
                entry.blocks[ new_block_index ] = block_state;
                entry.last_edit_time = std::chrono::steady_clock::now( );
                // (Re)schedule this chunk's idle-save timer.
                this->scheduleChunkSave( new_chunk_pos );
            }
        }
    }
}

void ServerLogic::respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset ) {

    const auto it_world = this->world_cache.find( chunk_offset );

    // The cache holds only chunks with unsaved edits; a hit is newer than
    // the save file. Unedited chunks stream their diffs straight from disk
    // without being cached.
    std::map<int, BlockState> disk_diffs;
    const std::map<int, BlockState> *chunk_cache_prt;
    // A timer-only entry (empty blocks) carries no merged diff — fall
    // through to the disk path like a cache miss.
    if ( it_world != this->world_cache.end( ) && !it_world->second.blocks.empty( ) ) {
        chunk_cache_prt = &it_world->second.blocks;
    } else {
        if ( this->known_empty_chunks.count( chunk_offset ) ) {
            // Known to have no diffs — skip the disk entirely.
            return;
        }
        this->loadDiffsFromDisk( chunk_offset, disk_diffs );
        if ( disk_diffs.empty( ) ) {
            if ( this->known_empty_chunks.size( ) >= KNOWN_EMPTY_CHUNKS_MAX ) {
                this->known_empty_chunks.clear( );
            }
            this->known_empty_chunks.insert( chunk_offset );
        }
        chunk_cache_prt = &disk_diffs;
    }
    const std::map<int, BlockState> &chunk_cache = *chunk_cache_prt;
    if ( chunk_cache.empty( ) ) {
        // No diffs for this chunk; emit no frame.
        return;
    }

    NetChunkDiffResultPayload diff;
    diff.chunk_x = chunk_offset.x;
    diff.chunk_y = chunk_offset.y;
    diff.chunk_z = chunk_offset.z;
    diff.diffs.clear( );
    diff.diffs.reserve( chunk_cache.size( ) );
    for ( auto [ blocks_index, blockState ] : chunk_cache ) {
        NetChunkDiffEntry entry;
        entry.blocks_index = static_cast<uint32_t>( blocks_index );
        entry.blockState = blockState;
        diff.diffs.push_back( entry );
    }
    diff.num_diffs = static_cast<uint32_t>( diff.diffs.size( ) );

    PacketWriter w;
    net_serialize_chunk_diff_result( w, diff );
    server.queue_message( client_fd, NetMsgType::CHUNK_DIFF_RESULT, 0, w.take_buf( ) );
}

void ServerLogic::persistChunk( const glm::ivec3 &chunk_pos ) {
    auto it = this->world_cache.find( chunk_pos );
    if ( it != this->world_cache.end( ) && !it->second.blocks.empty( ) ) {
        const std::map<int, BlockState> &chunk_cache = it->second.blocks;
        BlockState *blocks = ( BlockState * )malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) );
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
            blocks[ i ] = BLOCK_STATE_LAST_BLOCK_ID;
        }
        for ( auto [ chunk_index, blockState ] : chunk_cache ) {
            blocks[ chunk_index ] = blockState;
        }
        this->map_storage.persist_dirty_blocks( chunk_pos, blocks );
        free( blocks );
    }
    // Container sidecars are independent of the block cache — a chunk can
    // have container edits without block edits (pure inventory changes).
    this->containers.persist_chunk( chunk_pos );
}

void ServerLogic::persistAll( ) {
    this->map_storage.write_world_time( this->worldTimeNow( ) );
    this->entities.flush_all( );
    size_t saved = 0;
    for ( const auto &[ chunk_pos, entry ] : this->world_cache ) {
        if ( entry.blocks.empty( ) ) {
            continue;
        }
        this->persistChunk( chunk_pos );
        saved++;
    }
    // Container chunks may exist without a block-cache entry.
    this->containers.persist_all( );
    pr_debug( "World saved (%zu chunks)", saved );
    this->world_cache.clear( );
    this->wake_schedule.clear( );
}

void ServerLogic::flushAll( ) {
    this->persistAll( );
}

void ServerLogic::processTimers( Server &server ) {
    auto now = std::chrono::steady_clock::now( );
    while ( !this->wake_schedule.empty( ) && this->wake_schedule.begin( )->first <= now ) {
        auto it = this->wake_schedule.begin( );
        const glm::ivec3 chunk_pos = it->second;
        const auto deadline = it->first;
        this->wake_schedule.erase( it );
        // Stale slot from a reschedule/cancel: the entry's deadline is the
        // truth, a mismatch means this pop is already superseded.
        const auto eit = this->world_cache.find( chunk_pos );
        if ( eit == this->world_cache.end( ) || eit->second.wake_deadline != deadline ) {
            continue;
        }
        pr_debug( "Idle-saving chunk:%d %d %d", chunk_pos.x, chunk_pos.y, chunk_pos.z );
        this->persistChunk( chunk_pos );
        this->world_cache.erase( chunk_pos );
        // Container data survives eviction only while nobody is viewing it;
        // subscribers keep the entry (and its file stays saved) so an open
        // container remains editable.
        this->containers.evict_unwatched( chunk_pos );
    }
    // Furnace ticking runs at UPS_RATE, but only while some ticking
    // container can still change — an idle cached container doesn't earn a
    // wake. When a message wakes the loop anyway, up to 8 catch-up ticks run.
    this->containers.tick_due( server, now );
    if ( this->world_time_running && now >= this->next_time_broadcast ) {
        this->next_time_broadcast += std::chrono::seconds( WORLD_TIME_SYNC_SECONDS );
        // If we fell a full interval behind (long event loop stall), don't
        // fire a burst of catch-up broadcasts.
        if ( now >= this->next_time_broadcast ) {
            this->next_time_broadcast = now + std::chrono::seconds( WORLD_TIME_SYNC_SECONDS );
        }
        this->broadcastWorldTime( server );
    }
}

int ServerLogic::nextWakeTimeoutMs( ) const {
    auto wake = std::chrono::steady_clock::time_point::max( );
    if ( !this->wake_schedule.empty( ) ) {
        wake = this->wake_schedule.begin( )->first;
    }
    if ( this->world_time_running && this->next_time_broadcast < wake ) {
        wake = this->next_time_broadcast;
    }
    this->containers.next_wake( wake );
    if ( wake == std::chrono::steady_clock::time_point::max( ) ) {
        return -1;
    }
    auto now = std::chrono::steady_clock::now( );
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>( wake - now );
    long long ms = delta.count( );
    if ( ms < 0 ) {
        return 0;
    }
    if ( ms > INT_MAX ) {
        return INT_MAX;
    }
    return static_cast<int>( ms );
}

ChunkCacheEntry *ServerLogic::loadIntoCache( const glm::ivec3 &chunk_offset ) {
    // Only record_block reaches here, and it schedules an idle-save timer
    // right after — so every cache entry is persisted and evicted by
    // processTimers once its edits go quiet.
    ChunkCacheEntry &entry = this->world_cache[ chunk_offset ];
    entry.blocks.clear( );
    // This chunk is about to gain an edit — it's no longer known-empty.
    this->known_empty_chunks.erase( chunk_offset );
    this->loadDiffsFromDisk( chunk_offset, entry.blocks );
    return &entry;
}

bool ServerLogic::loadDiffsFromDisk( const glm::ivec3 &chunk_offset, std::map<int, BlockState> &out ) {
    out.clear( );
    // Check file existence before allocating a 350KB buffer. For a fresh
    // world where most chunks have no file, this avoids both a malloc/free
    // and a wasted fread attempt per chunk.
    if ( !this->map_storage.check_if_chunk_exists( chunk_offset ) ) {
        return false;
    }
    BlockState *blocks = ( BlockState * )malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        blocks[ i ] = BLOCK_STATE_LAST_BLOCK_ID;
    }
    int dirty = 0;
    int ret = this->map_storage.load_blocks( chunk_offset, blocks, dirty, true );
    if ( ret == 0 ) {
        free( blocks );
        // File existed according to check_if_chunk_exists but load_blocks
        // returned 0 (race or read error).
        return false;
    }
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        BlockState &blockState = blocks[ i ];
        if ( blockState.id != LAST_BLOCK_ID ) {
            out[ i ] = blockState;
        }
    }
    free( blocks );
    return true;
}

void ServerLogic::on_client_message( Server &server, int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {

    PacketReader r( payload.data( ), payload.size( ) );

    if ( this->entities.on_client_message( server, client_fd, type, payload ) ) {
        return;
    }

    if ( this->containers.on_client_message( server, client_fd, type, r ) ) {
        return;
    }

    if ( type == NetMsgType::BLOCK_UPDATE ) {
        NetBlockUpdatePayload bu;
        if ( !net_deserialize_block_update( r, bu ) ) {
            pr_debug( "Malformed BLOCK_UPDATE from:%d", client_fd );
            return;
        }
        glm::ivec3 block_pos = glm::ivec3( bu.x, bu.y, bu.z );
        // Learns/forgets container block states per kind and hands the
        // contents to the breaker when a container gets overwritten.
        this->containers.on_block_update( server, client_fd, block_pos, bu.blockState );
        this->record_block( block_pos, bu.blockState );

        // Forward to all other clients.
        PacketWriter w;
        net_serialize_block_update( w, bu );
        server.broadcast( NetMsgType::BLOCK_UPDATE, client_fd, w.buf( ), client_fd );
    } else if ( type == NetMsgType::CHUNK_DIFF_REQUEST ) {
        NetChunkDiffRequestPayload req;
        if ( !net_deserialize_chunk_diff_request( r, req ) ) {
            pr_debug( "Malformed CHUNK_DIFF_REQUEST from:%d", client_fd );
            return;
        }
        // Validate box dimensions.
        if ( req.size_x > NET_MAX_BOX_DIM || req.size_y > NET_MAX_BOX_DIM || req.size_z > NET_MAX_BOX_DIM ) {
            pr_debug( "CHUNK_DIFF_REQUEST box too large: %u x %u x %u from:%d", req.size_x, req.size_y, req.size_z, client_fd );
            return;
        }
        // Respond for each chunk in the box.
        for ( uint8_t dx = 0; dx < req.size_x; dx++ ) {
            for ( uint8_t dy = 0; dy < req.size_y; dy++ ) {
                for ( uint8_t dz = 0; dz < req.size_z; dz++ ) {
                    glm::ivec3 chunk_pos = glm::ivec3( req.min_x + dx, req.min_y + dy, req.min_z + dz );
                    this->respondToChunkRequest( server, client_fd, chunk_pos );
                }
            }
        }
    } else if ( type == NetMsgType::PLAYER_LOCATION ) {
        NetPlayerPayload p;
        if ( !net_deserialize_player( r, p ) ) {
            pr_debug( "Malformed PLAYER_LOCATION from:%d", client_fd );
            return;
        }
        this->entities.on_player_position( server, client_fd, p.x, p.y, p.z );
        // Forward to all other clients.
        PacketWriter w;
        net_serialize_player( w, p );
        server.broadcast( NetMsgType::PLAYER_LOCATION, client_fd, w.buf( ), client_fd );
    } else if ( type == NetMsgType::WORLD_TIME ) {
        NetWorldTimePayload p;
        if ( !net_deserialize_world_time( r, p ) ) {
            pr_debug( "Malformed WORLD_TIME from:%d", client_fd );
            return;
        }
        // A client set the time (e.g. PageUp/PgDn): adopt it as the new
        // clock base and forward to the other clients so everyone snaps to
        // the same value.
        this->world_time_base = p.world_time;
        this->world_time_anchor = std::chrono::steady_clock::now( );
        PacketWriter w;
        net_serialize_world_time( w, p );
        server.broadcast( NetMsgType::WORLD_TIME, client_fd, w.buf( ), client_fd );
    }
}

void ServerLogic::on_client_disconnected( Server &server, int client_fd ) {
    this->containers.on_client_disconnected( client_fd );
    this->entities.on_client_disconnected( server, client_fd );
    server.broadcast_empty( NetMsgType::PLAYER_DISCONNECTED, client_fd, client_fd );
    // If this was the last client, save everything to disk and free memory.
    // Otherwise, let the per-chunk idle timers handle persistence.
    bool any_connected = false;
    for ( int id = 0; id < MAX_CLIENT_FDS; id++ ) {
        if ( id == client_fd ) {
            continue;
        }
        if ( server.get_data_if_client_connected( id ) != NULL ) {
            any_connected = true;
            break;
        }
    }
    if ( !any_connected ) {
        // Freeze the world clock where it is; flushAll persists it.
        this->world_time_base = this->worldTimeNow( );
        this->world_time_running = false;
        pr_debug( "Last client disconnected; flushing all chunks to disk" );
        this->flushAll( );
    }
}
