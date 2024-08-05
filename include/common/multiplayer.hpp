#ifndef HEADER_MULTIPLAYER_H
#define HEADER_MULTIPLAYER_H

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "server/server.hpp"

class Multiplayer {
    friend class World;

    int sockfd;
    int portno;
    bool active;

    void set_block_send_packet( const NetPacket &update );

  public:
    void init( const char *hostname, int port );
    void cleanup( );

    void dostuff( int );

    void process_events( World &world );
    void set_block( const glm::ivec3 &block_pos, BlockState blockState );
    void update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation );
};

#endif
