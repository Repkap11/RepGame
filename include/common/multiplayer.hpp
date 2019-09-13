
#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "server/server.hpp"

void multiplayer_init( const char *hostname, int port );
void multiplayer_cleanup( );

void dostuff( int );

void multiplayer_process_events( World *world );
void multiplayer_set_block( int place, int block_x, int block_y, int block_z, BlockState blockState );
void multiplayer_update_players_position( float block_x, float block_y, float block_z, const glm::mat4 &rotation );
