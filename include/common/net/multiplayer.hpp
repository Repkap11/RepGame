
#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"

void multiplayer_init( );
void multiplayer_cleanup( );

void broadcast_chunk_update( int chunk_x, int chunk_y, int chunk_z, int block_x, int block_y, int block_z, BlockID blockID );
