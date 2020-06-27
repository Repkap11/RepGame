#include "common/RepGame.hpp"
#include "common/block.hpp"

inline void change_all_textures_to( unsigned short *face, BlockID new_texture ) {
    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
        face[ i ] = new_texture;
    }
}

#define REDSTONE_DUST_IF( name )                                                                                                                                                                                                               \
    if ( blockState->display_id == name ) {                                                                                                                                                                                                    \
        if ( blockState->current_redstone_power > 0 ) {                                                                                                                                                                                        \
            blockCoord->face[ FACE_TOP ] = name##_POWERED;                                                                                                                                                                                     \
        } else {                                                                                                                                                                                                                               \
            blockCoord->face[ FACE_TOP ] = name##_UNPOWERED;                                                                                                                                                                                   \
        }                                                                                                                                                                                                                                      \
    }

void block_adjust_coord_based_on_state( const Block *block, const BlockState *blockState, BlockCoords *blockCoord ) {
    if ( blockState->display_id == REDSTONE_LAMP_UNPOWERED && blockState->current_redstone_power > 0 ) {
        change_all_textures_to( blockCoord->face, REDSTONE_LAMP_POWERED );
    }

    REDSTONE_DUST_IF( REDSTONE_LINE_1 )
    REDSTONE_DUST_IF( REDSTONE_LINE_2 )
    REDSTONE_DUST_IF( REDSTONE_CROSS )

    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q1 )
    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q2 )
    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q3 )
    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q4 )
    // if ( blockState->display_id == REDSTONE_LINE_1 ) {
    //     if ( blockState->current_redstone_power > 0 ) {
    //         change_all_textures_to( blockCoord->face, REDSTONE_LINE_1_POWERED );
    //     } else {
    //         change_all_textures_to( blockCoord->face, REDSTONE_LINE_1_UNPOWERED );
    //     }
    // }
    // if ( blockState->display_id == REDSTONE_LINE_2 ) {
    //     if ( blockState->current_redstone_power > 0 ) {
    //         change_all_textures_to( blockCoord->face, REDSTONE_LINE_2_POWERED );
    //     } else {
    //         change_all_textures_to( blockCoord->face, REDSTONE_LINE_2_UNPOWERED );
    //     }
    // }
    // if ( blockState->display_id == REDSTONE_CROSS ) {
    //     if ( blockState->current_redstone_power > 0 ) {
    //         change_all_textures_to( blockCoord->face, REDSTONE_CROSS_POWERED );
    //     } else {
    //         change_all_textures_to( blockCoord->face, REDSTONE_CROSS_UNPOWERED );
    //     }
    // }
    if ( blockState->display_id == REDSTONE_TORCH && blockState->current_redstone_power > 0 ) {
        change_all_textures_to( blockCoord->face, REDSTONE_TORCH_OFF );
    }
}
