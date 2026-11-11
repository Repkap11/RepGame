// Transport-independent parts of Multiplayer: frame dispatch and the
// send-side helpers. The platform files (linux/, wasm/, windows/) provide
// init/process_events/cleanup plus the flush_outbound()/disconnect() hooks
// these functions call.

#include <stdio.h>
#include <string.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "common/multiplayer.hpp"
#include "common/net/mpload_trace.hpp"
#include "common/net/packet.hpp"

void Multiplayer::handle_frame( RepGameState &state, const std::vector<uint8_t> &payload ) {
    World &world = state.world;
    // Parse the fixed payload header: version(1) type(1) player_id(4)
    if ( payload.size( ) < 6 ) {
        pr_debug( "Received too-short frame payload: %zu", payload.size( ) );
        return;
    }
    const uint8_t version = payload[ 0 ];
    const NetMsgType type = static_cast<NetMsgType>( payload[ 1 ] );
    const int32_t player_id = static_cast<int32_t>(
        static_cast<uint32_t>( payload[ 2 ] )
        | ( static_cast<uint32_t>( payload[ 3 ] ) << 8 )
        | ( static_cast<uint32_t>( payload[ 4 ] ) << 16 )
        | ( static_cast<uint32_t>( payload[ 5 ] ) << 24 ) );
    if ( version != NET_PROTOCOL_VERSION ) {
        pr_debug( "Protocol version mismatch: got %u expected %u", version, NET_PROTOCOL_VERSION );
        this->mismatched_server_version = version;
        this->disconnect( );
        return;
    }
    if ( net_trace_enabled( ) ) {
        pr_debug( "net_in  %s from=%d n=%zu", net_msg_type_name( type ), player_id, payload.size( ) );
    }

    // Reader over the type-specific payload (after the 6-byte fixed header).
    PacketReader r( payload.data( ) + 6, payload.size( ) - 6 );

    // Entity-protocol messages route to the sync hub.
    if ( this->entities.handle( state, type, player_id, r ) ) {
        return;
    }
    // CONTAINER_* messages route to the container hub.
    if ( this->containers.handle( state, type, player_id, r ) ) {
        return;
    }

    switch ( type ) {
        case NetMsgType::CHUNK_DIFF_RESULT: {
            NetChunkDiffResultPayload diff;
            if ( !net_deserialize_chunk_diff_result( r, diff ) ) {
                pr_debug( "Malformed CHUNK_DIFF_RESULT, skipping" );
                return;
            }
            glm::ivec3 chunk_pos = glm::ivec3( diff.chunk_x, diff.chunk_y, diff.chunk_z );
            Chunk *chunk_prt = world.chunkLoader.get_chunk( chunk_pos );
            if ( chunk_prt == nullptr || chunk_prt->load_state ) {
                if ( mpload_trace_enabled( ) ) {
                    mpload_tracef( "recv chunk=%d,%d,%d n=%u bytes=%zu state=queued",
                                   chunk_pos.x, chunk_pos.y, chunk_pos.z, diff.num_diffs, payload.size( ) );
                }
                // Chunk isn't loaded yet (terrain gen still running, or
                // no chunk slot at this position). Queue the diff to apply
                // when the chunk finishes loading.
                this->queue_pending_diff( diff );
                return;
            }
            if ( mpload_trace_enabled( ) ) {
                mpload_tracef( "recv chunk=%d,%d,%d n=%u bytes=%zu state=live",
                               chunk_pos.x, chunk_pos.y, chunk_pos.z, diff.num_diffs, payload.size( ) );
            }
            Chunk &chunk = *chunk_prt;
            for ( uint32_t i = 0; i < diff.num_diffs; i++ ) {
                const NetChunkDiffEntry &entry = diff.diffs[ i ];
                if ( entry.blocks_index >= static_cast<uint32_t>( NET_CHUNK_BLOCK_SIZE ) ) {
                    pr_debug( "CHUNK_DIFF_RESULT bad blocks_index:%u (max:%d)", entry.blocks_index, NET_CHUNK_BLOCK_SIZE );
                    continue;
                }
                world.chunkLoader.apply_diff_block( chunk, static_cast<int>( entry.blocks_index ), &entry.blockState );
            }
            break;
        }
        case NetMsgType::BLOCK_UPDATE: {
            NetBlockUpdatePayload bu;
            if ( !net_deserialize_block_update( r, bu ) ) {
                pr_debug( "Malformed BLOCK_UPDATE, skipping" );
                return;
            }
            pr_debug( "Read message: block:%d", bu.blockState.id );
            glm::ivec3 block_pos = glm::ivec3( bu.x, bu.y, bu.z );
            glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
            Chunk *chunk_prt = world.chunkLoader.get_chunk( chunk_pos );
            if ( chunk_prt == nullptr || chunk_prt->load_state ) {
                // Loading or parked slot — there may be no block storage to
                // write (parking releases it). Queue the update as a pending
                // diff so it lands at the next load's finalize.
                NetChunkDiffResultPayload diff;
                diff.chunk_x = chunk_pos.x;
                diff.chunk_y = chunk_pos.y;
                diff.chunk_z = chunk_pos.z;
                NetChunkDiffEntry entry;
                entry.blocks_index = static_cast<uint32_t>( Chunk::get_index_from_coords( block_pos - chunk_pos * CHUNK_SIZE_I ) );
                entry.blockState = bu.blockState;
                diff.diffs.push_back( entry );
                diff.num_diffs = 1;
                this->queue_pending_diff( diff );
                break;
            }
            const BlockState prev_state = world.get_loaded_block( block_pos );
            world.set_loaded_block( block_pos, bu.blockState );
            // NO_DEBRIS marks in-place conversions and moves (grazes, tills,
            // grass spread/death, crop growth, lit swaps, piston moves) —
            // nothing broke, so no debris. Legacy unflagged writes keep the
            // old any-id-change behaviour.
            if ( ( bu.flags & NET_BU_FLAG_NO_DEBRIS ) == 0 ) {
                world.spawn_block_debris( block_pos, prev_state, bu.blockState.id );
            }
            break;
        }
        case NetMsgType::CLIENT_INIT: {
            NetPlayerPayload p;
            if ( !net_deserialize_player( r, p ) ) {
                pr_debug( "Malformed CLIENT_INIT, skipping" );
                return;
            }
            world.multiplayer_avatars.add( player_id );
            glm::mat4 rotation = glm::make_mat4( p.rotation );
            world.multiplayer_avatars.update_position( player_id, p.x, p.y, p.z, rotation, p.eye_height, p.flags, p.health, p.held_item, state.camera.pos );
            break;
        }
        case NetMsgType::PLAYER_LOCATION: {
            NetPlayerPayload p;
            if ( !net_deserialize_player( r, p ) ) {
                pr_debug( "Malformed PLAYER_LOCATION, skipping" );
                return;
            }
            glm::mat4 rotation = glm::make_mat4( p.rotation );
            world.multiplayer_avatars.update_position( player_id, p.x, p.y, p.z, rotation, p.eye_height, p.flags, p.health, p.held_item, state.camera.pos );
            break;
        }
        case NetMsgType::PLAYER_CONNECTED: {
            pr_debug( "Updating player connected:%d", player_id );
            world.multiplayer_avatars.add( player_id );
            break;
        }
        case NetMsgType::PLAYER_DISCONNECTED: {
            pr_debug( "Updating player disconected:%d", player_id );
            world.multiplayer_avatars.remove( player_id );
            break;
        }
        case NetMsgType::WORLD_TIME: {
            NetWorldTimePayload p;
            if ( !net_deserialize_world_time( r, p ) ) {
                pr_debug( "Malformed WORLD_TIME, skipping" );
                return;
            }
            // Snap to the server's authoritative clock. The tick loop keeps
            // advancing world_time locally; these syncs only correct drift.
            state.world_time = static_cast<long>( p.world_time );
            break;
        }
        default: {
            pr_debug( "Saw unexpected packet type:%d from:%d", static_cast<int>( type ), player_id );
            break;
        }
    }
}

void Multiplayer::set_block( const glm::ivec3 &block_pos, BlockState blockState, uint8_t flags ) {
    NetBlockUpdatePayload p;
    p.x = block_pos.x;
    p.y = block_pos.y;
    p.z = block_pos.z;
    p.blockState = blockState;
    p.flags = flags;
    this->send( NetMsgType::BLOCK_UPDATE, p );
}

void Multiplayer::request_chunk( const glm::ivec3 &chunk_pos ) {
    // Phase 1: send a 1x1x1 box. Phase 2 will batch via request_chunks_box.
    request_chunks_box( chunk_pos, 1, 1, 1 );
}

bool Multiplayer::request_chunks_box( const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz, bool initial ) {
    if ( !this->active ) {
        return false;
    }
    NetChunkDiffRequestPayload p;
    p.min_x = min.x;
    p.min_y = min.y;
    p.min_z = min.z;
    p.size_x = sx;
    p.size_y = sy;
    p.size_z = sz;
    this->send( NetMsgType::CHUNK_DIFF_REQUEST, p );
    if ( mpload_trace_enabled( ) ) {
        mpload_tracef( "req min=%d,%d,%d size=%d,%d,%d initial=%d",
                       min.x, min.y, min.z, ( int )sx, ( int )sy, ( int )sz, initial ? 1 : 0 );
    }
    return true;
}

void Multiplayer::send_payload( NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {
    if ( !this->active ) {
        return;
    }
    if ( net_trace_enabled( ) ) {
        pr_debug( "net_out %s id=%d n=%zu", net_msg_type_name( type ), player_id, payload.size( ) );
    }
    this->framed_socket.send_message( type, player_id, payload );
    this->flush_outbound( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error, disconnecting" );
        this->disconnect( );
    }
}

void Multiplayer::update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation, float eye_height, uint8_t flags, float health, ItemID held_item ) {
    if ( !this->active ) {
        return;
    }
    if ( prev_player_pos == player_pos && prev_rotation == rotation && prev_eye_height == eye_height && prev_flags == flags && prev_health == health && prev_held_item == held_item ) {
        return;
    }
    prev_player_pos = player_pos;
    prev_rotation = rotation;
    prev_eye_height = eye_height;
    prev_flags = flags;
    prev_health = health;
    prev_held_item = held_item;
    NetPlayerPayload p;
    p.x = player_pos.x;
    p.y = player_pos.y;
    p.z = player_pos.z;
    memcpy( p.rotation, glm::value_ptr( rotation ), sizeof( glm::mat4 ) );
    p.eye_height = eye_height;
    p.flags = flags;
    p.health = health;
    p.held_item = held_item;
    this->send( NetMsgType::PLAYER_LOCATION, p );
}

void Multiplayer::send_world_time( long world_time ) {
    NetWorldTimePayload p{ static_cast<int64_t>( world_time ) };
    this->send( NetMsgType::WORLD_TIME, p );
}

void Multiplayer::queue_pending_diff( const NetChunkDiffResultPayload &diff ) {
    glm::ivec3 chunk_pos = glm::ivec3( diff.chunk_x, diff.chunk_y, diff.chunk_z );
    // If a diff for this chunk is already pending (e.g. server sent two
    // frames for the same chunk), merge the new entries into the existing
    // pending diff. This is rare but handles it correctly.
    auto it = this->pending_diffs.find( chunk_pos );
    if ( it != this->pending_diffs.end( ) ) {
        for ( const auto &entry : diff.diffs ) {
            it->second.diffs.push_back( entry );
        }
        it->second.num_diffs = static_cast<uint32_t>( it->second.diffs.size( ) );
    } else {
        this->pending_diffs[ chunk_pos ] = diff;
    }
}

bool Multiplayer::apply_pending_diffs( ChunkLoader &chunkLoader, Chunk &chunk ) {
    auto it = this->pending_diffs.find( chunk.chunk_pos );
    if ( it == this->pending_diffs.end( ) ) {
        return false;
    }
    if ( mpload_trace_enabled( ) ) {
        mpload_tracef( "apply_pending chunk=%d,%d,%d n=%u",
                       chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z, it->second.num_diffs );
    }
    const NetChunkDiffResultPayload &diff = it->second;
    for ( uint32_t i = 0; i < diff.num_diffs; i++ ) {
        const NetChunkDiffEntry &entry = diff.diffs[ i ];
        if ( entry.blocks_index >= static_cast<uint32_t>( NET_CHUNK_BLOCK_SIZE ) ) {
            pr_debug( "Pending diff bad blocks_index:%u (max:%d)", entry.blocks_index, NET_CHUNK_BLOCK_SIZE );
            continue;
        }
        chunkLoader.apply_diff_block( chunk, static_cast<int>( entry.blocks_index ), &entry.blockState );
    }
    this->pending_diffs.erase( it );
    return true;
}
