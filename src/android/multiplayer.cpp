#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"

void multiplayer_init( const char *hostname, int port ) {
}

void multiplayer_process_events( World *world ) {
}

void multiplayer_set_block_send_packet( NetPacket *update ) {
}

void multiplayer_set_block( int place, int block_x, int block_y, int block_z, BlockState blockState ) {
}

void multiplayer_update_players_position( float player_x, float player_y, float player_z, const glm::mat4 &rotation ) {
}

void multiplayer_cleanup( ) {
}