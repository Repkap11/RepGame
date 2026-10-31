#include "common/RepGame.hpp"
#include "common/block.hpp"

inline void change_all_textures_to( unsigned short *face, TextureID new_texture ) {
    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
        face[ i ] = new_texture;
    }
}

#define REDSTONE_DUST_IF( name )                                                                                                                                                                                                               \
    if ( blockState->display_id == name ) {                                                                                                                                                                                                    \
        if ( blockState->current_redstone_power > 0 ) {                                                                                                                                                                                        \
            blockCoord->face[ FACE_TOP ] = TEX_##name##_POWERED;                                                                                                                                                                                     \
        } else {                                                                                                                                                                                                                               \
            blockCoord->face[ FACE_TOP ] = TEX_##name##_UNPOWERED;                                                                                                                                                                                   \
        }                                                                                                                                                                                                                                      \
    }

bool fence_connects_to( const Block *fence, BlockID neighbor_id, unsigned char neighbor_rotation, bool along_x ) {
    const Block *neighbor = block_definition_get_definition( neighbor_id );
    if ( neighbor->is_fence_gate ) {
        // A gate spans world x when ( rot & 1 ) == 0; it connects to fences
        // on the two sides perpendicular to its span.
        return ( ( neighbor_rotation & 1 ) == 0 ) == along_x;
    }
    if ( neighbor->is_fence ) {
        // Nether brick fences only connect to nether brick fences; wooden
        // fences connect to every other wooden fence.
        if ( fence->id == NETHER_BRICK_FENCE ) {
            return neighbor_id == NETHER_BRICK_FENCE;
        }
        return neighbor_id != NETHER_BRICK_FENCE;
    }
    return neighbor->is_solid;
}

// Rail thickness 2px centered on the post (z/x 7..9), heights 6..9 and 12..15.
// Both end faces of a rail are always hidden — the inner end sits against the
// post and the outer end touches the connected cell — so they skip drawing.
int fence_boxes( unsigned connections, BlockBox out[ FENCE_MAX_BOXES ] ) {
    int n = 0;
    out[ n ].scale[ 0 ] = 4;
    out[ n ].scale[ 1 ] = 16;
    out[ n ].scale[ 2 ] = 4;
    out[ n ].offset[ 0 ] = 6;
    out[ n ].offset[ 1 ] = 0;
    out[ n ].offset[ 2 ] = 6;
    out[ n ].skip_faces = 0;
    n++;
    const unsigned char skip_x = ( 1u << FACE_LEFT ) | ( 1u << FACE_RIGHT );
    const unsigned char skip_z = ( 1u << FACE_FRONT ) | ( 1u << FACE_BACK );
    if ( connections & ( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X ) ) {
        // Opposite connections merge into one rail spanning the whole cell.
        signed char lo = 0, hi = 16;
        if ( !( connections & FENCE_CONNECT_POS_X ) ) {
            hi = 6; // post face at x=6
        } else if ( !( connections & FENCE_CONNECT_NEG_X ) ) {
            lo = 10; // post face at x=10
        }
        for ( int rail = 0; rail < 2; rail++ ) {
            out[ n ].scale[ 0 ] = hi - lo;
            out[ n ].scale[ 1 ] = 3;
            out[ n ].scale[ 2 ] = 2;
            out[ n ].offset[ 0 ] = lo;
            out[ n ].offset[ 1 ] = rail == 0 ? 6 : 12;
            out[ n ].offset[ 2 ] = 7;
            out[ n ].skip_faces = skip_x;
            n++;
        }
    }
    if ( connections & ( FENCE_CONNECT_POS_Z | FENCE_CONNECT_NEG_Z ) ) {
        signed char lo = 0, hi = 16;
        if ( !( connections & FENCE_CONNECT_POS_Z ) ) {
            hi = 6;
        } else if ( !( connections & FENCE_CONNECT_NEG_Z ) ) {
            lo = 10;
        }
        for ( int rail = 0; rail < 2; rail++ ) {
            out[ n ].scale[ 0 ] = 2;
            out[ n ].scale[ 1 ] = 3;
            out[ n ].scale[ 2 ] = hi - lo;
            out[ n ].offset[ 0 ] = 7;
            out[ n ].offset[ 1 ] = rail == 0 ? 6 : 12;
            out[ n ].offset[ 2 ] = lo;
            out[ n ].skip_faces = skip_z;
            n++;
        }
    }
    return n;
}

// Collision boxes: the post plus one full-height 4px-thick bar per
// connected axis, spanning from the post face to the cell edge(s) — two
// fences one apart meet edge-to-edge so a 0.6-wide player can't slip
// between posts. Same merge rule as fence_boxes: both sides of an axis
// give one 0..16 bar. Boxes are 1.5 blocks tall (24px, Minecraft-style)
// so players and mobs can't hop fences.
int fence_collision_boxes( unsigned connections, BlockBox out[ FENCE_MAX_BOXES ] ) {
    int n = 0;
    out[ n ].scale[ 0 ] = 4;
    out[ n ].scale[ 1 ] = 24;
    out[ n ].scale[ 2 ] = 4;
    out[ n ].offset[ 0 ] = 6;
    out[ n ].offset[ 1 ] = 0;
    out[ n ].offset[ 2 ] = 6;
    out[ n ].skip_faces = 0;
    n++;
    if ( connections & ( FENCE_CONNECT_POS_X | FENCE_CONNECT_NEG_X ) ) {
        signed char lo = 0, hi = 16;
        if ( !( connections & FENCE_CONNECT_POS_X ) ) {
            hi = 6; // post face at x=6
        } else if ( !( connections & FENCE_CONNECT_NEG_X ) ) {
            lo = 10; // post face at x=10
        }
        out[ n ].scale[ 0 ] = hi - lo;
        out[ n ].scale[ 1 ] = 24;
        out[ n ].scale[ 2 ] = 4;
        out[ n ].offset[ 0 ] = lo;
        out[ n ].offset[ 1 ] = 0;
        out[ n ].offset[ 2 ] = 6;
        out[ n ].skip_faces = 0;
        n++;
    }
    if ( connections & ( FENCE_CONNECT_POS_Z | FENCE_CONNECT_NEG_Z ) ) {
        signed char lo = 0, hi = 16;
        if ( !( connections & FENCE_CONNECT_POS_Z ) ) {
            hi = 6;
        } else if ( !( connections & FENCE_CONNECT_NEG_Z ) ) {
            lo = 10;
        }
        out[ n ].scale[ 0 ] = 4;
        out[ n ].scale[ 1 ] = 24;
        out[ n ].scale[ 2 ] = hi - lo;
        out[ n ].offset[ 0 ] = 6;
        out[ n ].offset[ 1 ] = 0;
        out[ n ].offset[ 2 ] = lo;
        out[ n ].skip_faces = 0;
        n++;
    }
    return n;
}

// Definition space at rotation 0: the gate spans x, posts at x 0..2 and
// 14..16. Open leaves fold toward def -z (camera yaw 0 looks toward -z).
int fence_gate_boxes( bool open, BlockBox out[ FENCE_GATE_MAX_BOXES ] ) {
    int n = 0;
    for ( int post = 0; post < 2; post++ ) {
        out[ n ].scale[ 0 ] = 2;
        out[ n ].scale[ 1 ] = 11;
        out[ n ].scale[ 2 ] = 2;
        out[ n ].offset[ 0 ] = post == 0 ? 0 : 14;
        out[ n ].offset[ 1 ] = 5;
        out[ n ].offset[ 2 ] = 7;
        out[ n ].skip_faces = 0;
        n++;
    }
    if ( !open ) {
        // Rails x 2..14 at y 6..9 / 12..15, plus the center upright x 6..10,
        // y 6..15 — all z 7..9.
        for ( int rail = 0; rail < 2; rail++ ) {
            out[ n ].scale[ 0 ] = 12;
            out[ n ].scale[ 1 ] = 3;
            out[ n ].scale[ 2 ] = 2;
            out[ n ].offset[ 0 ] = 2;
            out[ n ].offset[ 1 ] = rail == 0 ? 6 : 12;
            out[ n ].offset[ 2 ] = 7;
            out[ n ].skip_faces = 0;
            n++;
        }
        out[ n ].scale[ 0 ] = 4;
        out[ n ].scale[ 1 ] = 9;
        out[ n ].scale[ 2 ] = 2;
        out[ n ].offset[ 0 ] = 6;
        out[ n ].offset[ 1 ] = 6;
        out[ n ].offset[ 2 ] = 7;
        out[ n ].skip_faces = 0;
        n++;
    } else {
        // Each leaf swings to the side of its post: rails z 3..7 and the
        // leaf edge upright z 1..3, at x 0..2 and x 14..16.
        for ( int leaf = 0; leaf < 2; leaf++ ) {
            const signed char lx = leaf == 0 ? 0 : 14;
            for ( int rail = 0; rail < 2; rail++ ) {
                out[ n ].scale[ 0 ] = 2;
                out[ n ].scale[ 1 ] = 3;
                out[ n ].scale[ 2 ] = 4;
                out[ n ].offset[ 0 ] = lx;
                out[ n ].offset[ 1 ] = rail == 0 ? 6 : 12;
                out[ n ].offset[ 2 ] = 3;
                out[ n ].skip_faces = 0;
                n++;
            }
            out[ n ].scale[ 0 ] = 2;
            out[ n ].scale[ 1 ] = 9;
            out[ n ].scale[ 2 ] = 2;
            out[ n ].offset[ 0 ] = lx;
            out[ n ].offset[ 1 ] = 6;
            out[ n ].offset[ 2 ] = 1;
            out[ n ].skip_faces = 0;
            n++;
        }
    }
    return n;
}

unsigned char fence_gate_toggle_rotation( unsigned char rotation, unsigned char player_yaw_rot ) {
    if ( rotation & BLOCK_ROTATE_GATE_OPEN ) {
        return static_cast<unsigned char>( rotation & ~BLOCK_ROTATE_GATE_OPEN );
    }
    unsigned char facing = rotation & 3;
    if ( ( player_yaw_rot & 1 ) == ( facing & 1 ) ) {
        facing = static_cast<unsigned char>( player_yaw_rot & 3 );
    }
    return static_cast<unsigned char>( facing | BLOCK_ROTATE_GATE_OPEN );
}

void block_adjust_coord_based_on_state( const Block *block, const BlockState *blockState, BlockCoords *blockCoord ) {
    if ( blockState->display_id == REDSTONE_LAMP && blockState->current_redstone_power > 0 ) {
        change_all_textures_to( blockCoord->face, TEX_REDSTONE_LAMP_ON );
    }

    REDSTONE_DUST_IF( REDSTONE_WIRE_LINE_1 )
    REDSTONE_DUST_IF( REDSTONE_WIRE_LINE_2 )
    REDSTONE_DUST_IF( REDSTONE_WIRE_CROSS )
    REDSTONE_DUST_IF( REDSTONE_WIRE_DOT )

    REDSTONE_DUST_IF( REDSTONE_WIRE_L_Q1 )
    REDSTONE_DUST_IF( REDSTONE_WIRE_L_Q2 )
    REDSTONE_DUST_IF( REDSTONE_WIRE_L_Q3 )
    REDSTONE_DUST_IF( REDSTONE_WIRE_L_Q4 )

    REDSTONE_DUST_IF( REDSTONE_WIRE_T_F )
    REDSTONE_DUST_IF( REDSTONE_WIRE_T_R )
    REDSTONE_DUST_IF( REDSTONE_WIRE_T_B )
    REDSTONE_DUST_IF( REDSTONE_WIRE_T_L )

    // if ( blockState->display_id == REDSTONE_WIRE_LINE_1 ) {
    //     if ( blockState->current_redstone_power > 0 ) {
    //         change_all_textures_to( blockCoord->face, REDSTONE_WIRE_LINE_1_POWERED );
    //     } else {
    //         change_all_textures_to( blockCoord->face, REDSTONE_WIRE_LINE_1_UNPOWERED );
    //     }
    // }
    // if ( blockState->display_id == REDSTONE_WIRE_LINE_2 ) {
    //     if ( blockState->current_redstone_power > 0 ) {
    //         change_all_textures_to( blockCoord->face, REDSTONE_WIRE_LINE_2_POWERED );
    //     } else {
    //         change_all_textures_to( blockCoord->face, REDSTONE_WIRE_LINE_2_UNPOWERED );
    //     }
    // }
    // if ( blockState->display_id == REDSTONE_WIRE_CROSS ) {
    //     if ( blockState->current_redstone_power > 0 ) {
    //         change_all_textures_to( blockCoord->face, REDSTONE_WIRE_CROSS_POWERED );
    //     } else {
    //         change_all_textures_to( blockCoord->face, REDSTONE_WIRE_CROSS_UNPOWERED );
    //     }
    // }
    if ( blockState->display_id == REDSTONE_TORCH && blockState->current_redstone_power == 0 ) {
        // Switch side faces to the off texture, but keep the top and bottom
        // using their dedicated textures.
        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            if ( i != FACE_TOP && i != FACE_BOTTOM ) {
                blockCoord->face[ i ] = TEX_REDSTONE_TORCH_OFF;
            }
        }
        blockCoord->face[ FACE_TOP ] = TEX_REDSTONE_TORCH_OFF_TOP;
        blockCoord->face[ FACE_BOTTOM ] = TEX_REDSTONE_TORCH_BOTTOM;
    }

    // Pressed activators get thinner: plates squash down, buttons sink into
    // their wall (the unrotated z axis is the attachment direction).
    if ( block->is_pressure_plate && blockState->current_redstone_power > 0 ) {
        blockCoord->scale_y = 1;
    } else if ( block->is_button && blockState->current_redstone_power > 0 ) {
        blockCoord->scale_z = 1;
    }

    if ( block->is_piston ) {
        const bool extended = blockState->current_redstone_power > 0;
        const unsigned char rot = blockState->rotation;
        if ( extended ) {
            // Extended: the body pulls back 4px from the head end so the throat
            // the arm slides through sits recessed. The shrink clips the
            // texture's UV window to [0, 0.75] on the thin axis, so each slot
            // needs the band variant with the wood band on the clipped
            // UV-high edge. Faces that already used such a variant keep their
            // tile (pixel-identical to the retracted face); faces that used a
            // UV-low band variant get the *_EXT tile — the same body pixels
            // shifted 4px, so every side keeps its retracted texture.
            const int head_slot = rot == PISTON_ROTATE_UP ? FACE_TOP : rot == PISTON_ROTATE_DOWN ? FACE_BOTTOM : FACE_FRONT;
            if ( rot == PISTON_ROTATE_UP || rot == PISTON_ROTATE_DOWN ) {
                // Vertical: all four laterals are v-thin (v runs along y).
                const TextureID side = rot == PISTON_ROTATE_UP ? TEX_PISTON_SIDE_UP : TEX_PISTON_SIDE_DOWN_EXT;
                blockCoord->face[ FACE_RIGHT ] = side;
                blockCoord->face[ FACE_LEFT ] = side;
                blockCoord->face[ FACE_FRONT ] = side;
                blockCoord->face[ FACE_BACK ] = side;
                blockCoord->face[ head_slot ] = TEX_PISTON_FRONT_EXTENDED;
                blockCoord->face[ head_slot == FACE_TOP ? FACE_BOTTOM : FACE_TOP ] = TEX_PISTON_BOTTOM;
            } else {
                // Horizontal: the shader rotates the TOP/BOTTOM texcoords with
                // the block yaw, so the thin axis always lands on the slot's
                // band edge — the assignment is rotation-invariant, matching
                // the retracted one with each UV-low-band tile swapped for its
                // *_EXT variant.
                blockCoord->face[ FACE_TOP ] = TEX_PISTON_SIDE_UP;
                blockCoord->face[ FACE_BOTTOM ] = TEX_PISTON_SIDE_DOWN_EXT;
                blockCoord->face[ FACE_RIGHT ] = TEX_PISTON_SIDE_LEFT;
                blockCoord->face[ FACE_LEFT ] = TEX_PISTON_SIDE_RIGHT_EXT;
                blockCoord->face[ FACE_FRONT ] = TEX_PISTON_FRONT_EXTENDED;
                blockCoord->face[ FACE_BACK ] = TEX_PISTON_BOTTOM;
            }
            if ( rot == PISTON_ROTATE_UP ) {
                blockCoord->scale_y = 12;
            } else if ( rot == PISTON_ROTATE_DOWN ) {
                blockCoord->scale_y = 12;
                blockCoord->offset_y = 4;
            } else {
                // The shader yaw-remaps scale_z/offset_z onto the facing axis.
                blockCoord->scale_z = 12;
            }
        } else if ( rot == PISTON_ROTATE_UP || rot == PISTON_ROTATE_DOWN ) {
            // Head faces straight up/down: the shader only yaws, so permute the
            // face textures here. All laterals get the same band variant, so
            // the shader's rot-5 lateral index shift is harmless.
            const TextureID face_tile = block->is_sticky_piston ? TEX_PISTON_TOP_STICKY : TEX_PISTON_TOP_NORMAL;
            const int head_slot = rot == PISTON_ROTATE_UP ? FACE_TOP : FACE_BOTTOM;
            const int back_slot = rot == PISTON_ROTATE_UP ? FACE_BOTTOM : FACE_TOP;
            const TextureID side = rot == PISTON_ROTATE_UP ? TEX_PISTON_SIDE_UP : TEX_PISTON_SIDE_DOWN;
            blockCoord->face[ head_slot ] = face_tile;
            blockCoord->face[ back_slot ] = TEX_PISTON_BOTTOM;
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
            const TextureID face_tile = block->is_sticky_piston ? TEX_PISTON_TOP_STICKY : TEX_PISTON_TOP_NORMAL;
            blockCoord->face[ FACE_FRONT ] = face_tile;
            blockCoord->face[ FACE_BACK ] = TEX_PISTON_BOTTOM;
            blockCoord->face[ FACE_TOP ] = TEX_PISTON_SIDE_UP;
            blockCoord->face[ FACE_BOTTOM ] = TEX_PISTON_SIDE_DOWN;
            blockCoord->face[ FACE_RIGHT ] = TEX_PISTON_SIDE_LEFT;
            blockCoord->face[ FACE_LEFT ] = TEX_PISTON_SIDE_RIGHT;
        }
    }

    if ( block->is_piston_head ) {
        // The plate's rim faces are 4px strips sampling only the tile's outer
        // quarter: rims thin in u see columns 0-3, rims thin in v see rows
        // 12-15. After the loader's X-flip those regions are wood bands on
        // tiles 115 (band at image right) and 114 (band at image bottom), so
        // each rim face gets the band tile matching its thin axis and reads
        // as a wooden plate edge. The head-end face keeps the head tile; the
        // plate's interior back face gets the plain body tile.
        const TextureID head_tile = block->id == STICKY_PISTON_ARM_COLLISION ? TEX_PISTON_TOP_STICKY : TEX_PISTON_TOP_NORMAL;
        const unsigned char rot = blockState->rotation;
        if ( rot == PISTON_ROTATE_UP || rot == PISTON_ROTATE_DOWN ) {
            // Horizontal plate — the shader only applies yaw to the canonical
            // forward-facing plate, so bake the vertical geometry here. Every
            // lateral slot lands on a rim thin in v.
            blockCoord->face[ FACE_TOP ] = rot == PISTON_ROTATE_UP ? head_tile : TEX_PISTON_BOTTOM;
            blockCoord->face[ FACE_BOTTOM ] = rot == PISTON_ROTATE_UP ? TEX_PISTON_BOTTOM : head_tile;
            blockCoord->face[ FACE_FRONT ] = TEX_PISTON_SIDE_DOWN;
            blockCoord->face[ FACE_BACK ] = TEX_PISTON_SIDE_DOWN;
            blockCoord->face[ FACE_RIGHT ] = TEX_PISTON_SIDE_DOWN;
            blockCoord->face[ FACE_LEFT ] = TEX_PISTON_SIDE_DOWN;
            blockCoord->scale_y = 4;
            blockCoord->scale_z = 16;
            blockCoord->offset_z = 0;
            blockCoord->offset_y = rot == PISTON_ROTATE_UP ? 12 : 0;
        } else {
            blockCoord->face[ FACE_FRONT ] = head_tile;
            blockCoord->face[ FACE_BACK ] = TEX_PISTON_BOTTOM;
            if ( rot == BLOCK_ROTATE_90 || rot == BLOCK_ROTATE_270 ) {
                // Plate thin in x: all four rim faces sample columns 0-3.
                blockCoord->face[ FACE_TOP ] = TEX_PISTON_SIDE_RIGHT;
                blockCoord->face[ FACE_BOTTOM ] = TEX_PISTON_SIDE_RIGHT;
                blockCoord->face[ FACE_RIGHT ] = TEX_PISTON_SIDE_RIGHT;
                blockCoord->face[ FACE_LEFT ] = TEX_PISTON_SIDE_RIGHT;
            } else {
                blockCoord->face[ FACE_TOP ] = TEX_PISTON_SIDE_DOWN;
                blockCoord->face[ FACE_BOTTOM ] = TEX_PISTON_SIDE_DOWN;
                blockCoord->face[ FACE_RIGHT ] = TEX_PISTON_SIDE_RIGHT;
                blockCoord->face[ FACE_LEFT ] = TEX_PISTON_SIDE_RIGHT;
            }
        }
    }
}
