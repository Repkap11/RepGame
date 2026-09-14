#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "common/multiplayer.hpp"
#include "common/net/packet.hpp"

void Multiplayer::init( const char *hostname, const int port ) {
    pr_debug( "using server %s:%i", hostname, port );

    this->active = false;
    this->portno = port;
    this->prev_player_pos = glm::vec3( 0.0f );
    this->prev_rotation = glm::mat4( 1.0f );

    // Create the socket on the main thread so we own its lifecycle. The
    // background connect thread uses this->sockfd but never closes it; on
    // shutdown cleanup() closes it to interrupt any pending poll().
    this->sockfd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( this->sockfd < 0 ) {
        pr_debug( "Unable to allocate socket" );
        this->sockfd = -1;
        return;
    }

    // Perform DNS + nonblocking connect + poll on a background thread so a
    // down/unreachable server cannot stall startup.
    this->connect_thread = std::thread( &Multiplayer::connect_async, this, std::string( hostname ) );
}

void Multiplayer::connect_async( const std::string &hostname ) {
    struct addrinfo hints;
    struct addrinfo *result = nullptr;

    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai_status = getaddrinfo( hostname.c_str( ), nullptr, &hints, &result );
    if ( gai_status != 0 ) {
        pr_debug( "Unable to resolve hostname: %s", gai_strerror( gai_status ) );
        return;
    }

    struct sockaddr_in serv_addr;
    memset( &serv_addr, 0, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    memcpy( &serv_addr.sin_addr, &reinterpret_cast<struct sockaddr_in *>( result->ai_addr )->sin_addr, sizeof( serv_addr.sin_addr ) );
    serv_addr.sin_port = htons( this->portno );
    freeaddrinfo( result );

    // Make the socket nonblocking before connect so we can bound the wait.
    int flags = fcntl( this->sockfd, F_GETFL );
    if ( flags < 0 ) {
        pr_debug( "Unable to get socket flags" );
        return;
    }
    if ( fcntl( this->sockfd, F_SETFL, flags | O_NONBLOCK ) < 0 ) {
        pr_debug( "Unable to set non-blocking" );
        return;
    }

    int rc = connect( this->sockfd, reinterpret_cast<sockaddr *>( &serv_addr ), sizeof( serv_addr ) );
    if ( rc == 0 ) {
        // Immediate success (e.g. localhost).
        this->framed_socket.adopt( this->sockfd );
        this->active = true;
        return;
    }
    if ( rc < 0 && errno != EINPROGRESS ) {
        pr_debug( "Multiplayer failed to connect: %s", strerror( errno ) );
        return;
    }

    // Wait for the socket to become writable, with a bounded timeout.
    struct pollfd pfd;
    pfd.fd = this->sockfd;
    pfd.events = POLLOUT;
    int pr = poll( &pfd, 1, MULTIPLAYER_CONNECT_TIMEOUT_MS );
    if ( pr <= 0 ) {
        if ( pr == 0 ) {
            pr_debug( "Multiplayer failed to connect: timed out after %d ms", MULTIPLAYER_CONNECT_TIMEOUT_MS );
        } else {
            pr_debug( "Multiplayer failed to connect: poll error: %s", strerror( errno ) );
        }
        return;
    }

    // Check whether the async connect actually succeeded.
    int so_error = 0;
    socklen_t so_len = sizeof( so_error );
    if ( getsockopt( this->sockfd, SOL_SOCKET, SO_ERROR, &so_error, &so_len ) < 0 ) {
        pr_debug( "Multiplayer failed to connect: getsockopt error: %s", strerror( errno ) );
        return;
    }
    if ( so_error != 0 ) {
        pr_debug( "Multiplayer failed to connect: %s", strerror( so_error ) );
        return;
    }

    // Connected. Adopt the fd into the FramedSocket and publish via the
    // atomic with release ordering so the game thread, on observing
    // active==true, also sees the initialized socket.
    this->framed_socket.adopt( this->sockfd );
    this->active = true;
}

void Multiplayer::process_events( World &world ) {
    if ( !this->active ) {
        return;
    }
    // Drain any pending sends first so the outbound queue doesn't grow unbounded.
    this->framed_socket.flush( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error, disconnecting" );
        this->active = false;
        close( this->sockfd );
        this->sockfd = -1;
        return;
    }

    int event_count = 0;
    while ( event_count < 100 ) {
        event_count++;
        auto payload = this->framed_socket.recv_message( );
        if ( this->framed_socket.had_error( ) ) {
            pr_debug( "Multiplayer recv error, disconnecting" );
            this->framed_socket.clear_error( );
            this->active = false;
            close( this->sockfd );
            this->sockfd = -1;
            return;
        }
        if ( !payload ) {
            // No more complete frames right now.
            return;
        }

        // Parse the fixed payload header: version(1) type(1) player_id(4)
        if ( payload->size( ) < 6 ) {
            pr_debug( "Received too-short frame payload: %zu", payload->size( ) );
            continue;
        }
        const uint8_t version = ( *payload )[ 0 ];
        const NetMsgType type = static_cast<NetMsgType>( ( *payload )[ 1 ] );
        const int32_t player_id = static_cast<int32_t>(
            static_cast<uint32_t>( ( *payload )[ 2 ] )
            | ( static_cast<uint32_t>( ( *payload )[ 3 ] ) << 8 )
            | ( static_cast<uint32_t>( ( *payload )[ 4 ] ) << 16 )
            | ( static_cast<uint32_t>( ( *payload )[ 5 ] ) << 24 ) );
        if ( version != NET_PROTOCOL_VERSION ) {
            pr_debug( "Protocol version mismatch: got %u expected %u", version, NET_PROTOCOL_VERSION );
            continue;
        }

        // Reader over the type-specific payload (after the 6-byte fixed header).
        PacketReader r( payload->data( ) + 6, payload->size( ) - 6 );

        switch ( type ) {
            case NetMsgType::CHUNK_DIFF_RESULT: {
                NetChunkDiffResultPayload diff;
                if ( !net_deserialize_chunk_diff_result( r, diff ) ) {
                    pr_debug( "Malformed CHUNK_DIFF_RESULT, skipping" );
                    continue;
                }
                glm::ivec3 chunk_pos = glm::ivec3( diff.chunk_x, diff.chunk_y, diff.chunk_z );
                Chunk *chunk_prt = world.chunkLoader.get_chunk( chunk_pos );
                if ( chunk_prt == nullptr ) {
                    // Chunk isn't loaded yet (terrain gen still running).
                    // Queue the diff to apply when the chunk finishes loading.
                    this->queue_pending_diff( diff );
                    continue;
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
                    continue;
                }
                pr_debug( "Read message: block:%d", bu.blockState.id );
                glm::ivec3 block_pos = glm::ivec3( bu.x, bu.y, bu.z );
                world.set_loaded_block( block_pos, bu.blockState );
                break;
            }
            case NetMsgType::CLIENT_INIT: {
                NetPlayerPayload p;
                if ( !net_deserialize_player( r, p ) ) {
                    pr_debug( "Malformed CLIENT_INIT, skipping" );
                    continue;
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
                    continue;
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
    this->framed_socket.flush( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error in set_block, disconnecting" );
        this->active = false;
        close( this->sockfd );
        this->sockfd = -1;
    }
}

void Multiplayer::request_chunk( const glm::ivec3 &chunk_pos ) {
    // Phase 1: send a 1x1x1 box. Phase 2 will batch via request_chunks_box.
    request_chunks_box( chunk_pos, 1, 1, 1 );
}

void Multiplayer::request_chunks_box( const glm::ivec3 &min, uint8_t sx, uint8_t sy, uint8_t sz ) {
    if ( !this->active ) {
        return;
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
    this->framed_socket.flush( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error in request_chunks_box, disconnecting" );
        this->active = false;
        close( this->sockfd );
        this->sockfd = -1;
    }
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
    this->framed_socket.flush( );
    if ( this->framed_socket.had_error( ) ) {
        pr_debug( "Multiplayer send error in update_players_position, disconnecting" );
        this->active = false;
        close( this->sockfd );
        this->sockfd = -1;
    }
}

void Multiplayer::cleanup( ) {
    pr_debug( "closing multiplayer" );
    // If the connect thread is still running (server unreachable, poll in
    // progress), close the fd to interrupt its poll(), then join. The connect
    // thread never closes the fd, so this is safe.
    if ( this->connect_thread.joinable( ) ) {
        if ( !this->active.load( ) && this->sockfd >= 0 ) {
            close( this->sockfd );
            this->sockfd = -1;
        }
        this->connect_thread.join( );
    }
    if ( this->active.load( ) && this->sockfd >= 0 ) {
        close( this->sockfd );
        this->sockfd = -1;
        this->active = false;
    }
    this->pending_diffs.clear( );
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
