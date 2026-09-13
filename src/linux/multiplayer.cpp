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

void Multiplayer::init( const char *hostname, const int port ) {
    pr_debug( "using server %s:%i", hostname, port );

    this->active = false;
    this->portno = port;
    this->pending_packet_len = 0;
    this->pending_send_len = 0;
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

    // Connected. Publish via the atomic with release ordering so the game
    // thread, on observing active==true, also sees the initialized sockfd.
    this->active = true;
}

void Multiplayer::process_events( World &world ) {
    int event_count = 0;
    if ( !this->active ) {
        // pr_debug( "Not this->active..." );
        return;
    }
    // Drain any pending sends first so the outbound queue doesn't grow unbounded.
    this->flush_send_queue( );
    while ( event_count < 100 ) {
        // Send updates to the server
        NetPacket &packet = this->pending_packet;

        int &pending_read_len = this->pending_packet_len;
        char *read_start = reinterpret_cast<char *>( &packet ) + pending_read_len;
        const size_t size_needed = sizeof( NetPacket ) - pending_read_len;

        event_count++;
        const int status = recv( this->sockfd, read_start, size_needed, 0 );
        if ( status < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // No more messages right now.
                return;
            }
            pr_debug( "recv error: %s", strerror( errno ) );
            this->active = false;
            close( this->sockfd );
            return;
        } else if ( status == 0 ) {
            // Server closed the connection.
            pr_debug( "Server disconnected" );
            this->active = false;
            close( this->sockfd );
            return;
        } else {
            pending_read_len += status;
            if ( pending_read_len < static_cast<int>( sizeof( NetPacket ) ) ) {
                continue;
            }
            pending_read_len = 0;
            if ( packet.type == PacketType::CHUNK_DIFF_RESULT ) {
                const PacketType_DataChunkDiff &chunk_diff = packet.data.chunk_diff;
                glm::ivec3 chunk_pos = glm::ivec3( chunk_diff.chunk_x, chunk_diff.chunk_y, chunk_diff.chunk_z );
                Chunk *chunk_prt = world.chunkLoader.get_chunk( chunk_pos );
                if ( chunk_prt == nullptr ) {
                    // This chunk is not loaded anymore, ignore this update.
                    continue;
                }
                Chunk &chunk = *chunk_prt;

                for ( int i = 0; i < packet.data.chunk_diff.num_used_updates; ++i ) {
                    const PacketType_DataChunkDiff_Block &net_block = packet.data.chunk_diff.blockUpdates[ i ];
                    chunk.set_block_by_index_if_different( net_block.blocks_index, &net_block.blockState );
                }
                // world.set_loaded_block();
            } else if ( packet.type == PacketType::BLOCK_UPDATE ) {
                BlockState &blockState = packet.data.block.blockState;
                pr_debug( "Read message: block:%d", blockState.id );
                glm::ivec3 block_pos = glm::ivec3( packet.data.block.x, packet.data.block.y, packet.data.block.z );
                world.set_loaded_block( block_pos, blockState );
            } else if ( packet.type == PacketType::CLIENT_INIT ) {
                world.multiplayer_avatars.add( packet.player_id );
                glm::mat4 rotation = glm::make_mat4( packet.data.player.rotation );
                world.multiplayer_avatars.update_position( packet.player_id, packet.data.player.x, packet.data.player.y, packet.data.player.z, rotation );
            } else if ( packet.type == PacketType::PLAYER_LOCATION ) {
                // pr_debug( "Updating player location:%d", update.player_id );
                glm::mat4 rotation = glm::make_mat4( packet.data.player.rotation );
                world.multiplayer_avatars.update_position( packet.player_id, packet.data.player.x, packet.data.player.y, packet.data.player.z, rotation );
            } else if ( packet.type == PacketType::PLAYER_CONNECTED ) {
                pr_debug( "Updating player connected:%d", packet.player_id );
                world.multiplayer_avatars.add( packet.player_id );
            } else if ( packet.type == PacketType::PLAYER_DISCONNECTED ) {
                pr_debug( "Updating player disconected:%d", packet.player_id );
                world.multiplayer_avatars.remove( packet.player_id );
            } else {
                pr_debug( "Saw unexpected packet:%d from:%d", packet.type, packet.player_id );
            }
        }
    }
}

void Multiplayer::flush_send_queue( ) {
    while ( !this->send_queue.empty( ) ) {
        NetPacket &packet = this->send_queue.front( );
        char *send_start = reinterpret_cast<char *>( &packet ) + this->pending_send_len;
        const size_t size_needed = sizeof( NetPacket ) - this->pending_send_len;

        int nsent = send( this->sockfd, send_start, size_needed, 0 );
        if ( nsent < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                // Socket send buffer is full; try again next frame.
                return;
            }
            pr_debug( "send error: %s", strerror( errno ) );
            this->active = false;
            close( this->sockfd );
            return;
        }
        this->pending_send_len += nsent;
        if ( this->pending_send_len < static_cast<int>( sizeof( NetPacket ) ) ) {
            // Partial write; the rest will be sent on the next flush.
            continue;
        }
        this->send_queue.pop( );
        this->pending_send_len = 0;
    }
}

void Multiplayer::send_packet( const NetPacket &update ) {
    if ( !this->active ) {
        return;
    }
    this->send_queue.push( update );
    this->flush_send_queue( );
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
    NetPacket update;
    update.type = PacketType::BLOCK_UPDATE;
    update.data.block.x = block_pos.x;
    update.data.block.y = block_pos.y;
    update.data.block.z = block_pos.z;
    memcpy( &update.data.block.blockState, &blockState, sizeof( BlockState ) );

    this->send_packet( update );
}

void Multiplayer::request_chunk( const glm::ivec3 &chunk_pos ) {
    if ( !this->active ) {
        return;
    }
    NetPacket update;
    update.type = PacketType::CHUNK_DIFF_REQUEST;
    update.data.chunk_diff.chunk_x = chunk_pos.x;
    update.data.chunk_diff.chunk_y = chunk_pos.y;
    update.data.chunk_diff.chunk_z = chunk_pos.z;
    this->send_packet( update );
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
    NetPacket update;
    update.type = PacketType::PLAYER_LOCATION;
    update.data.player.x = player_pos.x;
    update.data.player.y = player_pos.y;
    update.data.player.z = player_pos.z;
    memcpy( update.data.player.rotation, ( float * )glm::value_ptr( rotation ), sizeof( glm::mat4 ) );

    this->send_packet( update );
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
    while ( !this->send_queue.empty( ) ) {
        this->send_queue.pop( );
    }
}
