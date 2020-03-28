#include "common/RepGame.hpp"
#include "common/block.hpp"

inline void change_all_textures_to( unsigned short *face, BlockID new_texture ) {
    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
        face[ i ] = new_texture;
    }
}

void block_adjust_coord_based_on_state( const Block *block, const BlockState *blockState, BlockCoords *blockCoord ) {
    if ( blockState->id == REDSTONE_LAMP_UNPOWERED && blockState->current_redstone_power > 0 ) {
        change_all_textures_to( blockCoord->face, REDSTONE_LAMP_POWERED );
    }
    if ( blockState->id == REDSTONE_LINE_UNPOWERED && blockState->current_redstone_power > 0 ) {
        change_all_textures_to( blockCoord->face, REDSTONE_LINE_POWERED );
    }
    if ( blockState->id == REDSTONE_TORCH && blockState->current_redstone_power > 0 ) {
        change_all_textures_to( blockCoord->face, REDSTONE_TORCH_OFF );
    }
}
