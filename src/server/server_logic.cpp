
#include "server/server_logic.hpp"
#include "server/server.hpp"
#include "common/chunk.hpp"
#include "common/light.hpp"
#include "common/net/mpload_trace.hpp"
#include "common/net/packet.hpp"
#include "common/utils/env_config.hpp"

#include <algorithm>
#include <climits>
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

// Read a non-negative size cap from the environment; a negative value
// falls back to the default so a malformed setting can't disable or
// un-bound the structure.
static size_t env_size_cap( const char *name, long fallback ) {
    const long v = env_long( name, fallback );
    return v >= 0 ? static_cast<size_t>( v ) : static_cast<size_t>( fallback );
}

void ServerLogic::init( const char *world_name ) {
    this->map_storage.init( world_name );
    // Bounded diff payload cache (clean chunks only): byte budget in MB,
    // plus a hard entry cap for pathological tiny payloads. A 0 setting
    // disables the cache.
    this->diff_cache.set_limits( env_size_cap( "REPGAME_SERVER_DIFF_CACHE_MB", 16 ) * 1024 * 1024,
                                 env_size_cap( "REPGAME_SERVER_DIFF_CACHE_MAX", 65536 ) );
    // Cap on total dirty cells held in world_cache; 0 forces every edit to
    // persist+evict immediately.
    this->dirty_cells_max_ = env_size_cap( "REPGAME_SERVER_DIRTY_CELLS_MAX", 200000 );
    // The save-file index turns per-request existence checks into hash
    // lookups — a CHUNK_DIFF_REQUEST box otherwise stats every file in it.
    this->map_storage.enable_file_index( );
    // One-time migration: rewrite any chunk files still in the pre-v1
    // legacy format. Idempotent — a restarted server finds none.
    const int migrated = this->map_storage.migrate_legacy_chunk_files( );
    if ( migrated > 0 ) {
        pr_debug( "Migrated %d legacy chunk files to v1", migrated );
    }
    this->entities.init( &this->map_storage );
    this->containers.init( this );
    this->signs.init( this );
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
                // The chunk is going dirty: any cached clean payload is
                // now stale. Erased before world_cache changes so the two
                // stores can never overlap for one chunk.
                this->diff_cache.erase( new_chunk_pos );
                auto it_cache = this->world_cache.find( new_chunk_pos );
                // A chunk missing from the cache was idle-saved and evicted
                // (or never touched): merge its on-disk diff in first, else
                // this edit alone would overwrite the file on the next
                // persist and lose every earlier edit. A timer-only entry
                // (empty blocks, armed by a container edit) hasn't merged
                // either — route it through loadIntoCache too.
                ChunkCacheEntry *entry_p = ( it_cache != this->world_cache.end( ) && !it_cache->second.blocks.empty( )
                                                ? &it_cache->second
                                                : this->loadIntoCache( new_chunk_pos ) );
                if ( entry_p == nullptr ) {
                    // The file exists but won't decode: skip this write so the
                    // next persist can't rewrite it with only this session's
                    // edits and lose everything already on disk.
                    continue;
                }
                ChunkCacheEntry &entry = *entry_p;
                const size_t cells_before = entry.blocks.size( );
                entry.blocks[ new_block_index ] = block_state;
                this->dirty_cells_ += entry.blocks.size( ) - cells_before;
                entry.last_edit_time = std::chrono::steady_clock::now( );
                // (Re)schedule this chunk's idle-save timer.
                this->scheduleChunkSave( new_chunk_pos );
            }
        }
    }
    // The edit may have pushed total dirty state past the cap: persist and
    // evict the least-recently-edited chunks early rather than waiting out
    // the idle timers.
    this->enforceDirtyCap( );
}

// Serializes a chunk's diff map into CHUNK_DIFF_RESULT payload bytes. The
// live path and the DiffCache fill share this so a cached payload is
// byte-identical to a fresh encode.
static std::vector<uint8_t> encode_chunk_diff_result( const glm::ivec3 &chunk_offset, const std::map<int, BlockState> &diffs ) {
    NetChunkDiffResultPayload diff;
    diff.chunk_x = chunk_offset.x;
    diff.chunk_y = chunk_offset.y;
    diff.chunk_z = chunk_offset.z;
    diff.diffs.reserve( diffs.size( ) );
    for ( auto [ blocks_index, blockState ] : diffs ) {
        NetChunkDiffEntry entry;
        entry.blocks_index = static_cast<uint32_t>( blocks_index );
        entry.blockState = blockState;
        diff.diffs.push_back( entry );
    }
    diff.num_diffs = static_cast<uint32_t>( diff.diffs.size( ) );
    PacketWriter w;
    net_serialize_chunk_diff_result( w, diff );
    return w.take_buf( );
}

size_t ServerLogic::respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset ) {

    const auto it_world = this->world_cache.find( chunk_offset );

    // 1. A world_cache hit is authoritative — it holds edits newer than
    // the save file. (A timer-only entry with empty blocks carries no
    // merged diff and falls through like a miss.) Dirty chunks are encoded
    // fresh each request and never enter the DiffCache.
    if ( it_world != this->world_cache.end( ) && !it_world->second.blocks.empty( ) ) {
        std::vector<uint8_t> payload = encode_chunk_diff_result( chunk_offset, it_world->second.blocks );
        const size_t payload_bytes = payload.size( );
        server.queue_message( client_fd, NetMsgType::CHUNK_DIFF_RESULT, 0, payload );
        return payload_bytes;
    }

    // 2. DiffCache hit: the chunk is clean and the file already decoded —
    // replay the stored payload bytes untouched.
    if ( const std::vector<uint8_t> *cached = this->diff_cache.find( chunk_offset ) ) {
        server.queue_message( client_fd, NetMsgType::CHUNK_DIFF_RESULT, 0, *cached );
        return cached->size( );
    }

    // 3. Clean chunk: the file index answers "no file" in-memory, so a box
    // of fileless chunks costs hash lookups, not stats. A decoded
    // non-empty diff is encoded once and cached for the next request.
    std::map<int, BlockState> disk_diffs;
    this->loadDiffsFromDisk( chunk_offset, disk_diffs );
    if ( disk_diffs.empty( ) ) {
        // No diffs for this chunk; emit no frame.
        return 0;
    }
    std::vector<uint8_t> payload = encode_chunk_diff_result( chunk_offset, disk_diffs );
    const size_t payload_bytes = payload.size( );
    this->diff_cache.insert( chunk_offset, payload );
    server.queue_message( client_fd, NetMsgType::CHUNK_DIFF_RESULT, 0, payload );
    return payload_bytes;
}

void ServerLogic::persistChunk( const glm::ivec3 &chunk_pos ) {
    // The file is being (re)written — any cached payload for it is stale.
    // (In practice dirty chunks never reach the DiffCache; this is a
    // cheap guard against future writers forgetting to invalidate.)
    this->diff_cache.erase( chunk_pos );
    auto it = this->world_cache.find( chunk_pos );
    if ( it != this->world_cache.end( ) && !it->second.blocks.empty( ) ) {
        // The cache is a sorted index->state map: fold adjacent equal
        // states into v1 edit runs.
        std::vector<ChunkEditRun> runs;
        for ( const auto &[ chunk_index, blockState ] : it->second.blocks ) {
            if ( chunk_index < 0 || chunk_index >= CHUNK_BLOCK_SIZE ) {
                continue;
            }
            if ( !runs.empty( ) ) {
                ChunkEditRun &prev = runs.back( );
                if ( prev.start + prev.count == static_cast<uint32_t>( chunk_index ) && BlockStates_equal( prev.state, blockState ) ) {
                    prev.count++;
                    continue;
                }
            }
            runs.push_back( { static_cast<uint32_t>( chunk_index ), 1u, blockState } );
        }
        this->map_storage.persist_chunk_edits( chunk_pos, runs );
    }
    // Container sidecars are independent of the block cache — a chunk can
    // have container edits without block edits (pure inventory changes).
    this->containers.persist_chunk( chunk_pos );
    // Sign texts persist the same way — a pure text edit arms the same
    // idle timer on a timer-only entry.
    this->signs.persist_chunk( chunk_pos );
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
    this->signs.persist_all( );
    pr_debug( "World saved (%zu chunks)", saved );
    this->world_cache.clear( );
    this->wake_schedule.clear( );
    this->dirty_cells_ = 0;
    // Every file was just rewritten from memory — drop all cached
    // payloads; the next requests repopulate.
    this->diff_cache.clear( );
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
        this->evictChunk( eit );
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

void ServerLogic::evictChunk( std::map<glm::ivec3, ChunkCacheEntry, Compare_I_Vec3>::iterator it ) {
    const glm::ivec3 chunk_pos = it->first;
    this->persistChunk( chunk_pos );
    this->dirty_cells_ -= it->second.blocks.size( );
    this->world_cache.erase( it );
    // Container data survives eviction only while nobody is viewing it;
    // subscribers keep the entry (and its file stays saved) so an open
    // container remains editable.
    this->containers.evict_unwatched( chunk_pos );
    // Sign caches pin nothing — the file was just persisted by
    // persistChunk above, so the entry goes with the chunk.
    this->signs.evict_chunk( chunk_pos );
}

void ServerLogic::enforceDirtyCap( ) {
    if ( this->dirty_cells_ <= this->dirty_cells_max_ ) {
        return;
    }
    // Persist + evict the least-recently-edited chunks first. Timer-only
    // entries (empty blocks — a container edit armed the idle save) hold
    // no cells; evicting them wouldn't shrink the count, so they're
    // skipped and keep their armed deadline.
    std::vector<std::pair<std::chrono::steady_clock::time_point, glm::ivec3>> oldest_first;
    oldest_first.reserve( this->world_cache.size( ) );
    for ( const auto &[ chunk_pos, entry ] : this->world_cache ) {
        if ( !entry.blocks.empty( ) ) {
            oldest_first.emplace_back( entry.last_edit_time, chunk_pos );
        }
    }
    std::sort( oldest_first.begin( ), oldest_first.end( ),
               []( const auto &a, const auto &b ) {
                   // Same-timestamp ties still order deterministically
                   // (lowest chunk coords first) so the "oldest out" rule
                   // is stable for tests and busy edits.
                   return a.first != b.first ? a.first < b.first
                                             : Compare_I_Vec3( )( a.second, b.second );
               } );
    for ( const auto &[ edit_time, chunk_pos ] : oldest_first ) {
        if ( this->dirty_cells_ <= this->dirty_cells_max_ ) {
            break;
        }
        (void)edit_time;
        const auto it = this->world_cache.find( chunk_pos );
        if ( it == this->world_cache.end( ) || it->second.blocks.empty( ) ) {
            continue;
        }
        pr_debug( "Dirty-cap save: chunk %d %d %d (%zu cells over %zu)",
                  chunk_pos.x, chunk_pos.y, chunk_pos.z,
                  this->dirty_cells_, this->dirty_cells_max_ );
        this->evictChunk( it );
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
    const size_t cells_before = entry.blocks.size( );
    entry.blocks.clear( );
    if ( !this->loadDiffsFromDisk( chunk_offset, entry.blocks ) &&
         this->map_storage.check_if_chunk_exists( chunk_offset ) ) {
        // The file exists but wouldn't decode: merging nothing here would let
        // the next persist rewrite it with only this session's edits and lose
        // everything already on disk. Leave the entry empty and signal the
        // caller to skip this chunk instead.
        pr_debug( "ServerLogic: chunk file at %d %d %d won't decode — refusing to merge (on-disk edits preserved)",
                  chunk_offset.x, chunk_offset.y, chunk_offset.z );
        return nullptr;
    }
    this->dirty_cells_ += entry.blocks.size( ) - cells_before;
    return &entry;
}

bool ServerLogic::loadDiffsFromDisk( const glm::ivec3 &chunk_offset, std::map<int, BlockState> &out ) {
    out.clear( );
    // Check file existence before decoding — for a fresh world where most
    // chunks have no file, this avoids a wasted fopen attempt per chunk.
    if ( !this->map_storage.check_if_chunk_exists( chunk_offset ) ) {
        return false;
    }
    ChunkEdits edits;
    if ( this->map_storage.load_chunk_edits( chunk_offset, edits ) != 1 ) {
        // File existed according to check_if_chunk_exists but couldn't be
        // decoded (race or corruption): keep it rather than clobber.
        return false;
    }
    for ( const ChunkEditRun &run : edits.runs ) {
        for ( uint32_t i = run.start; i < run.start + run.count; i++ ) {
            out[ i ] = run.state;
        }
    }
    return true;
}

bool ServerLogic::recorded_block_at( const glm::ivec3 &pos, BlockState &out ) {
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    const glm::ivec3 diff = pos - chunk_pos * CHUNK_SIZE_I;
    const int index = Chunk::get_index_from_coords( diff );
    const auto it = this->world_cache.find( chunk_pos );
    if ( it != this->world_cache.end( ) && !it->second.blocks.empty( ) ) {
        // A cached entry already holds the merged disk diff — a miss here
        // is authoritative (the cell was never edited), no disk fallback.
        const auto bit = it->second.blocks.find( index );
        if ( bit == it->second.blocks.end( ) ) {
            return false;
        }
        out = bit->second;
        return true;
    }
    std::map<int, BlockState> diffs;
    this->loadDiffsFromDisk( chunk_pos, diffs );
    const auto bit = diffs.find( index );
    if ( bit == diffs.end( ) ) {
        return false;
    }
    out = bit->second;
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

    if ( this->signs.on_client_message( server, client_fd, type, r ) ) {
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
        // Same for sign text: an overwritten sign drops its stored text
        // (broadcast to every client).
        this->signs.on_block_update( server, block_pos, bu.blockState );
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
        const bool trace = mpload_trace_enabled( );
        const double req_t0 = trace ? mpload_now_ms( ) : 0.0;
        if ( trace ) {
            mpload_tracef( "srv_req fd=%d min=%d,%d,%d size=%d,%d,%d",
                           client_fd, req.min_x, req.min_y, req.min_z,
                           ( int )req.size_x, ( int )req.size_y, ( int )req.size_z );
        }
        // Serve nearest-first from the requester's player chunk (the box
        // centre when no PLAYER_LOCATION has arrived yet) so the chunks
        // around the player land ahead of the periphery.
        std::vector<glm::ivec3> order;
        order.reserve( static_cast<size_t>( req.size_x ) * req.size_y * req.size_z );
        for ( uint8_t dx = 0; dx < req.size_x; dx++ ) {
            for ( uint8_t dy = 0; dy < req.size_y; dy++ ) {
                for ( uint8_t dz = 0; dz < req.size_z; dz++ ) {
                    order.emplace_back( req.min_x + dx, req.min_y + dy, req.min_z + dz );
                }
            }
        }
        glm::dvec3 ref;
        glm::ivec3 player_chunk;
        if ( this->entities.player_chunk_of( client_fd, player_chunk ) ) {
            ref = glm::dvec3( player_chunk );
        } else {
            ref = glm::dvec3( req.min_x + ( req.size_x - 1 ) * 0.5,
                              req.min_y + ( req.size_y - 1 ) * 0.5,
                              req.min_z + ( req.size_z - 1 ) * 0.5 );
        }
        std::stable_sort( order.begin( ), order.end( ),
                          [ &ref ]( const glm::ivec3 &a, const glm::ivec3 &b ) {
                              const glm::dvec3 da = glm::dvec3( a ) - ref;
                              const glm::dvec3 db = glm::dvec3( b ) - ref;
                              return glm::dot( da, da ) < glm::dot( db, db );
                          } );
        int frames_queued = 0;
        size_t bytes_queued = 0;
        for ( const glm::ivec3 &chunk_pos : order ) {
            const size_t n = this->respondToChunkRequest( server, client_fd, chunk_pos );
            bytes_queued += n;
            frames_queued += n > 0 ? 1 : 0;
            // Sign text rides the same request — one SIGN_TEXT per stored
            // sign, for chunks with and without block diffs alike (viewers
            // need sign text without ever opening anything).
            this->signs.respondToChunkRequest( server, client_fd, chunk_pos );
        }
        if ( trace ) {
            mpload_tracef( "srv_served fd=%d chunks=%d frames=%d bytes=%zu us=%lld",
                           client_fd, ( int )req.size_x * ( int )req.size_y * ( int )req.size_z,
                           frames_queued, bytes_queued,
                           ( long long )( ( mpload_now_ms( ) - req_t0 ) * 1000.0 ) );
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
