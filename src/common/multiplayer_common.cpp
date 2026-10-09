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
#include "common/net/packet.hpp"

void Multiplayer::handle_frame( World &world, const std::vector<uint8_t> &payload ) {
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
        return;
    }

    // Reader over the type-specific payload (after the 6-byte fixed header).
    PacketReader r( payload.data( ) + 6, payload.size( ) - 6 );

    switch ( type ) {
        case NetMsgType::CHUNK_DIFF_RESULT: {
            NetChunkDiffResultPayload diff;
            if ( !net_deserialize_chunk_diff_result( r, diff ) ) {
                pr_debug( "Malformed CHUNK_DIFF_RESULT, skipping" );
                return;
            }
            glm::ivec3 chunk_pos = glm::ivec3( diff.chunk_x, diff.chunk_y, diff.chunk_z );
            Chunk *chunk_prt = world.chunkLoader.get_chunk( chunk_pos );
            if ( chunk_prt == nullptr || chunk_prt->is_loading ) {
                // Chunk isn't loaded yet (terrain gen still running, or
                // no chunk slot at this position). Queue the diff to apply
                // when the chunk finishes loading.
                this->queue_pending_diff( diff );
                return;
            }
            Chunk &chunk = *chunk_prt;
            for ( uint32_t i = 0; i < diff.num_diffs; i++ ) {
                const NetChunkDiffEntry &entry = diff.diffs[ i ];
                if ( entry.blocks_index >= static_cast<uint32_t>( NET_CHUNK_BLOCK_SIZE ) ) {
                    pr_debug( "CHUNK_DIFF_RESULT bad blocks_index:%u (max:%d)", entry.blocks_index, NET_CHUNK_BLOCK_SIZE );
                    continue;
                }
                chunk.set_block_by_index_if_different( static_cast<int>( entry.blocks_index ), &entry.blockState );
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
            const BlockState prev_state = world.get_loaded_block( block_pos );
            world.set_loaded_block( block_pos, bu.blockState );
            world.spawn_block_debris( block_pos, prev_state, bu.blockState.id );
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
            world.multiplayer_avatars.update_position( player_id, p.x, p.y, p.z, rotation );
            break;
        }
        case NetMsgType::PLAYER_LOCATION: {
            NetPlayerPayload p;
            if ( !net_deserialize_player( r, p ) ) {
                pr_debug( "Malformed PLAYER_LOCATION, skipping" );
                return;
            }
            glm::mat4 rotation = glm::make_mat4( p.rotation );
            world.multiplayer_avatars.update_position( player_id, p.x, p.y, p.z, rotation );
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
        default: {
            pr_debug( "Saw unexpected packet type:%d from:%d", static_cast<int>( type ), player_id );
            break;
        }
    }
}

void Multiplayer::set_block( const glm::ivec3 &block_pos, BlockState blockState ) {
    if ( !this->active ) {
        return;
    }
    // Log what the player is doing
    if ( blockState.id != AIR ) {
        // pr_debug( "Player placed %i at %i, %i, %i", blockState.id, block_x, block_y, block_z );
    } else {
        // pr_debug( "Player broke the block at %i, %i, %i", block_x, block_y, block_z );
    }
    NetBlockUpdatePayload p;
    p.x = block_pos.x;
    p.y = block_pos.y;
    p.z = block_pos.z;
    p.blockState = blockState;

    PacketWriter w;
    net_serialize_block_update( w, p );
    this->framed_socket.send_message( NetMsgType::BLOCK_UPDATE, 0, w.take_buf( ) );
    this->flush_outbound( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error in set_block, disconnecting" );
        this->disconnect( );
    }
}

void Multiplayer::request_chunk( const glm::ivec3 &chunk_pos ) {
    // Phase 1: send a 1x1x1 box. Phase 2 will batch via request_chunks_box.
    request_chunks_box( chunk_pos, 1, 1, 1 );
}

bool Multiplayer::request_chunks_box( const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz ) {
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

    PacketWriter w;
    net_serialize_chunk_diff_request( w, p );
    this->framed_socket.send_message( NetMsgType::CHUNK_DIFF_REQUEST, 0, w.take_buf( ) );
    this->flush_outbound( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error in request_chunks_box, disconnecting" );
        this->disconnect( );
    }
    return true;
}

void Multiplayer::update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation ) {
    if ( !this->active ) {
        return;
    }
    if ( prev_player_pos == player_pos && prev_rotation == rotation ) {
        return;
    }
    prev_player_pos = player_pos;
    prev_rotation = rotation;
    NetPlayerPayload p;
    p.x = player_pos.x;
    p.y = player_pos.y;
    p.z = player_pos.z;
    memcpy( p.rotation, glm::value_ptr( rotation ), sizeof( glm::mat4 ) );

    PacketWriter w;
    net_serialize_player( w, p );
    this->framed_socket.send_message( NetMsgType::PLAYER_LOCATION, 0, w.take_buf( ) );
    this->flush_outbound( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error in update_players_position, disconnecting" );
        this->disconnect( );
    }
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

void Multiplayer::apply_pending_diffs( Chunk &chunk ) {
    auto it = this->pending_diffs.find( chunk.chunk_pos );
    if ( it == this->pending_diffs.end( ) ) {
        return;
    }
    const NetChunkDiffResultPayload &diff = it->second;
    for ( uint32_t i = 0; i < diff.num_diffs; i++ ) {
        const NetChunkDiffEntry &entry = diff.diffs[ i ];
        if ( entry.blocks_index >= static_cast<uint32_t>( NET_CHUNK_BLOCK_SIZE ) ) {
            pr_debug( "Pending diff bad blocks_index:%u (max:%d)", entry.blocks_index, NET_CHUNK_BLOCK_SIZE );
            continue;
        }
        chunk.set_block_by_index_if_different( static_cast<int>( entry.blocks_index ), &entry.blockState );
    }
    this->pending_diffs.erase( it );
}
