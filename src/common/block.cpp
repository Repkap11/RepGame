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
    REDSTONE_DUST_IF( REDSTONE_DOT )

    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q1 )
    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q2 )
    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q3 )
    REDSTONE_DUST_IF( REDSTONE_DUST_L_Q4 )

    REDSTONE_DUST_IF( REDSTONE_DUST_T_F )
    REDSTONE_DUST_IF( REDSTONE_DUST_T_R )
    REDSTONE_DUST_IF( REDSTONE_DUST_T_B )
    REDSTONE_DUST_IF( REDSTONE_DUST_T_L )

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
    if ( blockState->display_id == REDSTONE_TORCH && blockState->current_redstone_power == 0 ) {
        // Switch side faces to the off texture, but keep the top and bottom
        // using their dedicated textures.
        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            if ( i != FACE_TOP && i != FACE_BOTTOM ) {
                blockCoord->face[ i ] = REDSTONE_TORCH_OFF;
            }
        }
        blockCoord->face[ FACE_TOP ] = REDSTONE_TORCH_OFF_TOP;
        blockCoord->face[ FACE_BOTTOM ] = REDSTONE_TORCH_BOTTOM;
    }

    // Pressed activators get thinner: plates squash down, buttons sink into
    // their wall (the unrotated z axis is the attachment direction).
    if ( block->is_pressure_plate && blockState->current_redstone_power > 0 ) {
        blockCoord->scale_y = 1;
    } else if ( block->is_button && blockState->current_redstone_power > 0 ) {
        blockCoord->scale_z = 1;
    }

    if ( block->is_piston ) {
        // The head-end face texture: the retracted look is the head tile, the
        // extended look is the throat/hole the arm slides through.
        const BlockID face_tile = blockState->current_redstone_power > 0 ? PISTON_FRONT_EXTENDED
                                                                       : ( block->is_sticky_piston ? PISTON_HEAD_STICKY_FACE : PISTON_HEAD_FACE );
        const unsigned char rot = blockState->rotation;
        if ( rot == PISTON_ROTATE_UP || rot == PISTON_ROTATE_DOWN ) {
            // Head faces straight up/down: the shader only yaws, so permute the
            // face textures here. All laterals get the same band variant, so
            // the shader's rot-5 lateral index shift is harmless.
            const int head_slot = rot == PISTON_ROTATE_UP ? FACE_TOP : FACE_BOTTOM;
            const int back_slot = rot == PISTON_ROTATE_UP ? FACE_BOTTOM : FACE_TOP;
            const BlockID side = rot == PISTON_ROTATE_UP ? PISTON_SIDE_UP : PISTON_SIDE_DOWN;
            blockCoord->face[ head_slot ] = face_tile;
            blockCoord->face[ back_slot ] = PISTON_BACK;
            blockCoord->face[ FACE_RIGHT ] = side;
            blockCoord->face[ FACE_LEFT ] = side;
            blockCoord->face[ FACE_FRONT ] = side;
            blockCoord->face[ FACE_BACK ] = side;
        } else {
            // Horizontal (rot 0-3): the shader's face-shift lands FACE_FRONT's
            // texture on the head-facing world face. The lateral slots never
            // need per-rotation variants: as the rotation permutes which
            // geometric face a slot lands on, the band edge each slot needs
            // stays the same (RIGHT slot's band edge is always u1, LEFT's u0;
            // TOP always wants the band at v1, BOTTOM at v0 — the shader
            // rotates top/bottom texcoords with the block).
            blockCoord->face[ FACE_FRONT ] = face_tile;
            blockCoord->face[ FACE_BACK ] = PISTON_BACK;
            blockCoord->face[ FACE_TOP ] = PISTON_SIDE_UP;
            blockCoord->face[ FACE_BOTTOM ] = PISTON_SIDE_DOWN;
            blockCoord->face[ FACE_RIGHT ] = PISTON_SIDE_LEFT;
            blockCoord->face[ FACE_LEFT ] = PISTON_SIDE_RIGHT;
        }
    }

    if ( block->is_piston_head && blockState->rotation >= PISTON_ROTATE_UP ) {
        // Head facing straight up/down: the shader only applies yaw to the
        // canonical forward-facing plate, so bake the vertical plate here.
        blockCoord->face[ blockState->rotation == PISTON_ROTATE_UP ? FACE_TOP : FACE_BOTTOM ] = blockCoord->face[ FACE_FRONT ];
        blockCoord->face[ FACE_FRONT ] = PISTON_HEAD_SIDE;
        blockCoord->scale_y = 4;
        blockCoord->scale_z = 16;
        blockCoord->offset_z = 0;
        blockCoord->offset_y = blockState->rotation == PISTON_ROTATE_UP ? 12 : 0;
    }
}
