#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"

void Multiplayer::init( const char *hostname, int port ) {
}

void Multiplayer::process_events( World &world ) {
}

void Multiplayer::set_block_send_packet( const NetPacket &update ) {
}

void Multiplayer::set_block( const glm::ivec3 &block_pos, BlockState blockState ) {
}

void Multiplayer::update_players_position( const glm::vec3 &player_pos, const glm::mat4 &rotation ) {
}

void Multiplayer::cleanup( ) {
}