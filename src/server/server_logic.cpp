
#include "server/server_logic.hpp"
#include "common/utils/file_utils.hpp"
#include "common/RepGame.hpp"
#include "common/net/packet.hpp"

#include <climits>
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerLogic::init( const char *world_name ) {
    this->map_storage.init( world_name );
}

void ServerLogic::on_client_connected( Server &server, int client_fd ) {
    pr_debug( "%d", client_fd );

    // Tell the other clients about this new client
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            continue;
        }
        if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
            pr_debug( "Notifying existing player:%d of new player:%d", prop_id, client_fd );
            server.queue_empty( prop_id, NetMsgType::PLAYER_CONNECTED, client_fd );
        }
    }

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
            PacketWriter w;
            net_serialize_player( w, p );
            server.queue_message( client_fd, NetMsgType::CLIENT_INIT, online_id, w.take_buf( ) );
            pr_debug( "Notifying new player:%d of existing player:%d", client_fd, online_id );
        }
    }
}

void ServerLogic::cancelChunkSave( const glm::ivec3 &chunk_pos ) {
    auto it = this->chunk_wake.find( chunk_pos );
    if ( it == this->chunk_wake.end( ) ) {
        return;
    }
    // Remove the old entry from wake_schedule. There may be multiple chunks
    // at the same wake time, so find the one matching chunk_pos.
    auto range = this->wake_schedule.equal_range( it->second );
    for ( auto i = range.first; i != range.second; ++i ) {
        if ( i->second == chunk_pos ) {
            this->wake_schedule.erase( i );
            break;
        }
    }
    this->chunk_wake.erase( it );
}

void ServerLogic::scheduleChunkSave( const glm::ivec3 &chunk_pos ) {
    // Cancel any pending timer for this chunk, then schedule a new one.
    this->cancelChunkSave( chunk_pos );
    auto wake = std::chrono::steady_clock::now( ) + std::chrono::seconds( CHUNK_IDLE_SAVE_SECONDS );
    this->wake_schedule.insert( { wake, chunk_pos } );
    this->chunk_wake[ chunk_pos ] = wake;
}

void ServerLogic::record_block( const glm::ivec3 &block_pos, BlockState &block_state ) {
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
    glm::ivec3 diff = block_pos - ( chunk_pos * CHUNK_SIZE_I );
    int block_index = Chunk::get_index_from_coords( diff );

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
                auto &entry = this->world_cache[ new_chunk_pos ];
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

    ChunkCacheEntry *chunk_cache_prt;
    if ( it_world == this->world_cache.end( ) ) {
        chunk_cache_prt = this->loadIntoCache( chunk_offset );
    } else {
        chunk_cache_prt = &it_world->second;
    }
    if ( chunk_cache_prt == NULL ) {
        // We don't have any blocks for this chunk.
        return;
    }
    const std::map<int, BlockState> &chunk_cache = chunk_cache_prt->blocks;
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
    if ( it == this->world_cache.end( ) ) {
        return;
    }
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

void ServerLogic::persistAll( ) {
    if ( this->world_cache.empty( ) ) {
        return;
    }
    for ( auto [ chunk_pos, entry ] : this->world_cache ) {
        this->persistChunk( chunk_pos );
    }
    pr_debug( "World saved (%zu chunks)", this->world_cache.size( ) );
    this->world_cache.clear( );
    this->wake_schedule.clear( );
    this->chunk_wake.clear( );
}

void ServerLogic::flushAll( ) {
    this->persistAll( );
}

void ServerLogic::processTimers( ) {
    auto now = std::chrono::steady_clock::now( );
    while ( !this->wake_schedule.empty( ) && this->wake_schedule.begin( )->first <= now ) {
        auto it = this->wake_schedule.begin( );
        glm::ivec3 chunk_pos = it->second;
        this->wake_schedule.erase( it );
        this->chunk_wake.erase( chunk_pos );
        pr_debug( "Idle-saving chunk:%d %d %d", chunk_pos.x, chunk_pos.y, chunk_pos.z );
        this->persistChunk( chunk_pos );
        this->world_cache.erase( chunk_pos );
    }
}

int ServerLogic::nextWakeTimeoutMs( ) const {
    if ( this->wake_schedule.empty( ) ) {
        return -1;
    }
    auto now = std::chrono::steady_clock::now( );
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>( this->wake_schedule.begin( )->first - now );
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
    // Check file existence before allocating a 350KB buffer. For a fresh
    // world where most chunks have no file, this avoids both a malloc/free
    // and a wasted fread attempt per chunk.
    if ( !this->map_storage.check_if_chunk_exists( chunk_offset ) ) {
        // Cache the "no file" result so subsequent requests for this chunk
        // skip the disk check entirely.
        ChunkCacheEntry &entry = this->world_cache[ chunk_offset ];
        entry.blocks.clear( );
        return &entry;
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
        // returned 0 (race or read error). Cache as empty to avoid retrying.
        ChunkCacheEntry &entry = this->world_cache[ chunk_offset ];
        entry.blocks.clear( );
        return &entry;
    }
    ChunkCacheEntry &entry = this->world_cache[ chunk_offset ];
    // pr_debug( "found chunk:%d %d %d size:%ld", chunk_offset.x, chunk_offset.y, chunk_offset.z, entry.blocks.size( ) );
    entry.blocks.clear( );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        BlockState &blockState = blocks[ i ];
        if ( blockState.id != LAST_BLOCK_ID ) {
            entry.blocks[ i ] = blockState;
        }
    }
    free( blocks );
    // Loaded from disk but not yet edited — no idle timer needed. It will
    // be evicted on the next flushAll() (SIGINT / last-client-disconnect).
    return &entry;
}

void ServerLogic::on_client_message( Server &server, int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {

    PacketReader r( payload.data( ), payload.size( ) );

    if ( type == NetMsgType::BLOCK_UPDATE ) {
        NetBlockUpdatePayload bu;
        if ( !net_deserialize_block_update( r, bu ) ) {
            pr_debug( "Malformed BLOCK_UPDATE from:%d", client_fd );
            return;
        }
        glm::ivec3 block_pos = glm::ivec3( bu.x, bu.y, bu.z );
        this->record_block( block_pos, bu.blockState );

        // Forward to all other clients.
        PacketWriter w;
        net_serialize_block_update( w, bu );
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                continue;
            }
            if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
                server.queue_message( prop_id, NetMsgType::BLOCK_UPDATE, client_fd, w.buf( ) );
            }
        }
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
        // Forward to all other clients.
        PacketWriter w;
        net_serialize_player( w, p );
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                continue;
            }
            if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
                server.queue_message( prop_id, NetMsgType::PLAYER_LOCATION, client_fd, w.buf( ) );
            }
        }
    }
}

void ServerLogic::on_client_disconnected( Server &server, int client_fd ) {
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            continue;
        }
        if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
            server.queue_empty( prop_id, NetMsgType::PLAYER_DISCONNECTED, client_fd );
            pr_debug( "Notifying existing player:%d of removed player:%d", prop_id, client_fd );
        }
    }
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
        pr_debug( "Last client disconnected; flushing all chunks to disk" );
        this->flushAll( );
    }
}
