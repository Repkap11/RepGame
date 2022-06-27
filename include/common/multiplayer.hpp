
#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "server/server.hpp"

void multiplayer_init( const char *hostname, int port );
void multiplayer_cleanup( );

void dostuff( int );

void multiplayer_process_events( World *world );
void multiplayer_set_block( const glm::ivec3 &block_pos, BlockState blockState );
void multiplayer_update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation );