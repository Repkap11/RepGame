#include "common/block_mesh_emit.hpp"

#include "common/fluid.hpp"
#include "common/mesh_gen.hpp"
#define MG_QUAL static inline
#include "common/mesh_gen_fns.hpp"
#include "common/rail.hpp"
#include "common/repeater.hpp"

// Per-cell BlockCoords emission shared by chunk meshing (block_emit_cell
// replaces chunk.cpp's old phase-2 emit block) and block bodies, which call
// it with a fractional origin, mesh size 1, an all-BRIGHT light set and
// cull_against_neighbors = false. The code below is the verbatim port of
// that emit block — keep the two in lockstep when touching shapes.

// Multi-box models: fences read the 4 horizontal neighbour cells for their
// connection mask, stairs for the corner derive; gates and signs are
// neighbour-independent.
static int emit_shape_boxes( const BlockEmitParams &p, BlockBox boxes[ FENCE_GATE_MAX_BOXES ] ) {
    const Block *block = p.block;
    const BlockState &blockState = *p.state;
    if ( block->is_fence ) {
        unsigned connections = 0;
        const int conn_dir[ 4 ][ 4 ] = { // dx, dz, connect bit, along_x
            { 1, 0, FENCE_CONNECT_POS_X, 1 },
            { -1, 0, FENCE_CONNECT_NEG_X, 1 },
            { 0, 1, FENCE_CONNECT_POS_Z, 0 },
            { 0, -1, FENCE_CONNECT_NEG_Z, 0 },
        };
        for ( int d = 0; d < 4; d++ ) {
            const BlockState &ncell = p.nb.at( conn_dir[ d ][ 0 ], 0, conn_dir[ d ][ 1 ] );
            if ( fence_connects_to( block, ncell.id, ncell.rotation, conn_dir[ d ][ 3 ] ) ) {
                connections |= conn_dir[ d ][ 2 ];
            }
        }
        return fence_boxes( connections, boxes );
    }
    if ( block->is_wall ) {
        // Same gather as the fence's plus the cell above
        // (post raise / tall arms) — the halo covers y±1
        // like the rail/below reads.
        const unsigned mask = wall_connection_mask( [ & ]( int dx, int dy, int dz ) { return p.nb.at( dx, dy, dz ); } );
        return wall_boxes( mask, boxes );
    }
    if ( block->is_fence_gate ) {
        return fence_gate_boxes( ( blockState.rotation & BLOCK_ROTATE_GATE_OPEN ) != 0, boxes );
    }
    if ( block->is_stairs ) {
        // Neighbor stair rotations for the corner derive
        // (stair_rots[d] = rotation of the stair in world
        // direction d, -1 = not a stair) — same gather as
        // collision.cpp's is_stairs branch.
        static const int stair_dir[ 4 ][ 2 ] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
        int stair_rots[ 4 ];
        for ( int d = 0; d < 4; d++ ) {
            const BlockState &ncell = p.nb.at( stair_dir[ d ][ 0 ], 0, stair_dir[ d ][ 1 ] );
            stair_rots[ d ] = block_definition_get_definition( ncell.id )->is_stairs ? ncell.rotation : -1;
        }
        return stairs_boxes( stairs_shape_derive( blockState.rotation, stair_rots ),
                             ( blockState.rotation & BLOCK_ROTATE_STAIRS_TOP ) != 0, boxes );
    }
    if ( block->is_standing_sign ) {
        // Board plate + post — def space, yawed by the
        // shader like the fence/stair boxes.
        return standing_sign_boxes( boxes );
    }
    if ( block->is_hanging_sign ) {
        // Board plate + chain strands — same def-space,
        // shader-yawed path.
        return hanging_sign_boxes( boxes );
    }
    return 0;
}

bool block_emit_needs_neighbors( const Block *block ) {
    return block->is_fence || block->is_wall || block->is_stairs || block->is_rail || block->is_redstone_dust || block->spine_shell;
}

// The extra-instance flags the emit count needs — computed once per emit.
static bool emit_redstone_orb( const BlockEmitParams &p ) {
    return p.state->display_id == REDSTONE_TORCH && p.state->current_redstone_power > 0;
}
static bool emit_piston_arm( const BlockEmitParams &p ) {
    return p.block->is_piston_head || ( p.block->is_piston && p.state->current_redstone_power > 0 );
}

int block_emit_count( const BlockEmitParams &p ) {
    const Block *block = p.block;
    BlockBox boxes[ FENCE_GATE_MAX_BOXES ];
    const int num_boxes = emit_shape_boxes( p, boxes );
    return ( num_boxes > 0 ? num_boxes : 1 ) + ( emit_redstone_orb( p ) ? 1 : 0 ) + ( block->spine_shell ? 2 : 0 ) +
           ( emit_piston_arm( p ) ? p.mesh_x * p.mesh_y * p.mesh_z : 0 ) +
           ( block->is_repeater ? ( p.state->current_redstone_power > 0 ? 4 : 2 ) : 0 );
}

int block_emit_cell( const BlockEmitParams &p, const BlockEmitSink &sink ) {
    const Block *block = p.block;
    const BlockState &blockState = *p.state;
    const int size_x = p.mesh_x;
    const int size_y = p.mesh_y;
    const int size_z = p.mesh_z;
    int emitted = 0;
    const auto alloc = [ & ]( ) -> BlockCoords * {
        BlockCoords *c = sink.alloc( sink.ctx );
        if ( c != nullptr ) {
            emitted++;
        }
        return c;
    };

    // Fences and gates emit several box instances per
    // cell instead of the single scaled cube: the fence
    // post plus rails for each connected side (connection
    // mask from the 4 horizontal neighbor cells — the
    // halo is valid there, the same reads the
    // redstone-dust code does), or the gate's closed/open
    // definition-space model.
    BlockBox boxes[ FENCE_GATE_MAX_BOXES ];
    const int num_boxes = emit_shape_boxes( p, boxes );

    // A lit redstone torch emits a second instance: the
    // 4-wide orb head around the stick's top.
    const bool redstone_orb = emit_redstone_orb( p );
    const bool piston_arm_cell = emit_piston_arm( p );

    BlockCoords *blockCoord = alloc( );
    if ( blockCoord == nullptr ) {
        return emitted;
    }

    blockCoord->x = p.origin.x;
    blockCoord->y = p.origin.y;
    blockCoord->z = p.origin.z;

    blockCoord->mesh_x = static_cast<unsigned char>( size_x );
    blockCoord->mesh_y = static_cast<unsigned char>( size_y );
    blockCoord->mesh_z = static_cast<unsigned char>( size_z );
    blockCoord->face_shift = block_yaw( block, blockState.rotation );
    bool rail_ascending = false;
    if ( block->is_rail ) {
        // The rail's rotation is its shape, not a yaw:
        // rail_render_yaw turns the straight/turned
        // tile onto the track, and ascending cells flag
        // the vertex shader to raise the uphill end.
        const RailShape rail_shape = rail_shape_of( blockState );
        rail_ascending = rail_is_ascending( rail_shape );
        blockCoord->face_shift = rail_render_yaw( rail_shape ) |
                                 ( rail_ascending ? BLOCK_COORDS_SLOPE : 0u );
    }
    blockCoord->scale_x = block->scale.x;
    blockCoord->scale_y = block->scale.y;
    blockCoord->scale_z = block->scale.z;

    blockCoord->offset_x = block->offset.x;
    blockCoord->offset_y = block->offset.y;
    blockCoord->offset_z = block->offset.z;

    blockCoord->tex_offset_x = block->tex_offset.x;
    blockCoord->tex_offset_y = block->tex_offset.y;
    blockCoord->tex_offset_z = block->tex_offset.z;

    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
        blockCoord->packed_lighting[ i ] = p.packed_lighting[ i ];
        blockCoord->face[ i ] = block->textures[ i ];
    }
    block_adjust_coord_based_on_state( block, &blockState, blockCoord );

    // A rail's underside shows wherever the support
    // doesn't fully cover the cell floor — through
    // glass/slab/stair supports, and a slope's tilted
    // back (top-lit in the shader, like MC's two-sided
    // slope quad). Give every rail's bottom face the
    // (possibly powered) top tile, but cull a flat
    // rail's underside over an opaque support: it sits
    // coplanar with the support's top face and would
    // z-fight — the same cullface test phase 1 uses.
    if ( block->is_rail ) {
        blockCoord->face[ FACE_BOTTOM ] = blockCoord->face[ FACE_TOP ];
        if ( p.cull_against_neighbors && !rail_ascending ) {
            const GPUBlockDef *mesh_defs = mesh_def_table( );
            const BlockState &below_cell = p.nb.at( 0, -1, 0 );
            const int below_rot = below_cell.rotation;
            const int below_face = OPPOSITE_FACE[ mesh_rotated_face( FACE_BOTTOM, below_rot ) ];
            if ( !mesh_face_seethrough( mesh_defs[ below_cell.id ], below_cell.current_redstone_power != 0,
                                        below_rot, below_face ) ) {
                blockCoord->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
            }
        }
    }

    if ( block_is_fluid( block ) ) {
        // Sloped-fluid metadata: yaw 0 with the fluid
        // flag (rotation carries the sim level, not a
        // yaw), and the precomputed corner heights ride
        // the free BlockCoords bytes. A non-flat surface
        // swaps its top to the flow texture — the vertex
        // shader rotates the top UVs onto the flow.
        const unsigned int corners = p.fluid_corners;
        const bool fluid_flat = corners == FLUID_CORNERS_FLAT;
        blockCoord->face_shift = BLOCK_COORDS_FLUID;
        blockCoord->fluid_h0 = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( corners & 0xffu );
        blockCoord->fluid_h[ 0 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 8 ) & 0xffu );
        blockCoord->fluid_h[ 1 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 16 ) & 0xffu );
        blockCoord->fluid_h[ 2 ] = fluid_flat ? FLUID_SOURCE_HEIGHT : static_cast<unsigned char>( ( corners >> 24 ) & 0xffu );
        if ( !fluid_flat ) {
            blockCoord->face[ FACE_TOP ] = blockCoord->face[ FACE_FRONT ];
        }
    }

    // Render redstone dust on the sides of dust blocks for vertical connections.
    // When dust has air to the side and dust below that air, the dust's side face
    // is visible and shows a dust strip on the side of the solid block below.
    if ( block->is_redstone_dust ) {
        // Use LINE_2 (perpendicular to LINE_1) so the line appears vertical on side faces
        TextureID side_texture = blockState.current_redstone_power > 0 ? TEX_REDSTONE_WIRE_LINE_2_POWERED : TEX_REDSTONE_WIRE_LINE_2_UNPOWERED;
        const int horiz_dx[ 4 ] = { 1, -1, 0, 0 };
        const int horiz_dz[ 4 ] = { 0, 0, 1, -1 };
        const int horiz_face[ 4 ] = { FACE_RIGHT, FACE_LEFT, FACE_FRONT, FACE_BACK };
        bool has_vertical_connection = false;
        for ( int d = 0; d < 4; d++ ) {
            const BlockState &neighbor = p.nb.at( horiz_dx[ d ], 0, horiz_dz[ d ] );
            const Block *neighbor_block = block_definition_get_definition( neighbor.display_id );
            // Only render side face when neighbor is non-solid and not dust (air)
            if ( !neighbor_block->collides_with_player && !neighbor_block->is_redstone_dust ) {
                const BlockState &below = p.nb.at( horiz_dx[ d ], -1, horiz_dz[ d ] );
                const Block *below_block = block_definition_get_definition( below.display_id );
                if ( below_block->is_redstone_dust ) {
                    blockCoord->face[ horiz_face[ d ] ] = side_texture;
                    blockCoord->packed_lighting[ horiz_face[ d ] ] = NO_LIGHT_BRIGHT;
                    has_vertical_connection = true;
                }
            }
        }
        // Extend the dust block downward to cover the solid block below,
        // so the side face is visible as a full-height strip.
        // Outset slightly (1 pixel) so the dust face renders in front of
        // the solid block's face, preventing z-fighting and ensuring visibility.
        // tex_offset is set to keep the texture centered (opposite of centered_border).
        if ( has_vertical_connection ) {
            blockCoord->scale_y = 16;
            blockCoord->offset_y = -15;
            blockCoord->scale_x = 18;
            blockCoord->offset_x = -1;
            blockCoord->scale_z = 18;
            blockCoord->offset_z = -1;
            blockCoord->tex_offset_x = 1;
            blockCoord->tex_offset_z = 1;
        }
    }

    // Offset torches that are mounted on the side of blocks.
    // rotation 4=attached LEFT, 5=attached FRONT, 6=attached RIGHT, 7=attached BACK
    // Push the torch towards the wall it's attached to.
    if ( block->is_torch && blockState.rotation >= 4 ) {
        // Torch scale is (2, 10, 2), offset is (7, 0, 7)
        // Push towards the wall: set offset to 0 on the wall side,
        // or 14 (=16-2) on the opposite side
        if ( blockState.rotation == 4 ) {
            // Attached LEFT: push to left wall
            blockCoord->offset_x = 0;
        } else if ( blockState.rotation == 5 ) {
            // Attached FRONT: push to back (towards wall at z+1)
            blockCoord->offset_z = 14;
        } else if ( blockState.rotation == 6 ) {
            // Attached RIGHT: push to right wall
            blockCoord->offset_x = 14;
        } else if ( blockState.rotation == 7 ) {
            // Attached BACK: push to front (towards wall at z-1)
            blockCoord->offset_z = 0;
        }
    }

    // Lit redstone torches get Java's
    // template_redstone_torch glow: a hollow 3x3x3 shell
    // around the stick's top whose quads face inward
    // (BLOCK_COORDS_INSIDE_OUT), so back-face culling
    // leaves the far walls as a red glow behind the
    // stick. It copies the stick instance (lighting,
    // face_shift) and follows the wall-mount shift via
    // offset * 2 - 1, with a fixed tex_offset like the
    // stick. BLOCK_COORDS_HALF_PIXEL gives the half-pixel
    // placement Java uses (6.5 -> 13 in 1/32 units).
    if ( redstone_orb ) {
        BlockCoords *orb = alloc( );
        if ( orb != nullptr ) {
            *orb = *blockCoord;
            orb->face_shift = ( blockCoord->face_shift & BLOCK_COORDS_ROTATION_MASK ) | BLOCK_COORDS_INSIDE_OUT | BLOCK_COORDS_HALF_PIXEL;
            orb->scale_x = 6;
            orb->scale_y = 6;
            orb->scale_z = 6;
            orb->offset_x = blockCoord->offset_x * 2 - 1;
            orb->offset_y = 15;
            orb->offset_z = blockCoord->offset_z * 2 - 1;
            orb->tex_offset_x = -13;
            orb->tex_offset_y = -15;
            orb->tex_offset_z = -13;
            for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                orb->face[ f ] = TEX_REDSTONE_TORCH_GLOW;
            }
        }
    }

    // A repeater cell emits two torch-stick instances on
    // top of its slab, matching Java's unrotated
    // repeater_Ntick model (output toward -z, our rotation
    // 0): the fixed torch sits at the output end, the
    // movable one slides back with the 1-4 delay. The
    // shader yaws the def-space boxes with the cell. The
    // sticks sample the torch tiles' top 5 stick rows;
    // the bottom cap is skipped and every quad is forced
    // lit like the piston arm, since the quads sit inside
    // the cell. A powered repeater's torches also get the
    // lit-torch glow shell (same inside-out trick as the
    // redstone_orb, sized to the stub's top).
    if ( block->is_repeater ) {
        const bool lit = blockState.current_redstone_power > 0;
        const int torch_z[ 2 ] = { 2, 6 + 2 * ( repeater_delay( blockState ) - 1 ) };
        for ( int i = 0; i < 2; i++ ) {
            BlockCoords *torch = alloc( );
            if ( torch == nullptr ) {
                break;
            }
            *torch = *blockCoord;
            torch->scale_x = 2;
            torch->scale_y = 5;
            torch->scale_z = 2;
            torch->offset_x = 7;
            torch->offset_y = 2;
            torch->offset_z = static_cast<signed char>( torch_z[ i ] );
            torch->tex_offset_x = -7;
            torch->tex_offset_y = -5;
            torch->tex_offset_z = -7;
            for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                torch->packed_lighting[ f ] = NO_LIGHT_BRIGHT;
                torch->face[ f ] = lit ? TEX_REDSTONE_TORCH_ON : TEX_REDSTONE_TORCH_OFF;
            }
            torch->face[ FACE_TOP ] = lit ? TEX_REDSTONE_TORCH_TOP : TEX_REDSTONE_TORCH_OFF_TOP;
            torch->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
            if ( lit ) {
                BlockCoords *orb = alloc( );
                if ( orb == nullptr ) {
                    break;
                }
                *orb = *torch;
                orb->face_shift = ( torch->face_shift & BLOCK_COORDS_ROTATION_MASK ) | BLOCK_COORDS_INSIDE_OUT | BLOCK_COORDS_HALF_PIXEL;
                orb->scale_x = 6;
                orb->scale_y = 6;
                orb->scale_z = 6;
                orb->offset_x = torch->offset_x * 2 - 1;
                orb->offset_y = 9;
                orb->offset_z = torch->offset_z * 2 - 1;
                orb->tex_offset_x = -13;
                orb->tex_offset_y = -9;
                orb->tex_offset_z = -13;
                for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                    orb->face[ f ] = TEX_REDSTONE_TORCH_GLOW;
                }
            }
        }
    }

    // Piston heads emit a second instance per run cell:
    // the 4x4 arm spanning from the plate back to the
    // cell's base end. Horizontal heads use the
    // definition-space shaft (the shader yaws it with the
    // plate); up/down get the world-baked column the same
    // way block_adjust bakes the plate. Laterals use the
    // wood arm tile (Bedrock pistonArm.png's board style:
    // planks framed in steel) and both end caps are
    // skipped — the head end is buried in the plate, the
    // base end meets the throat stub the extended base
    // emits. Plate runs merge across cells, so the arms
    // loop the run volume to give every merged cell its
    // own shaft.
    if ( block->is_piston_head ) {
        const int head_face = piston_head_face( blockState.rotation );
        const unsigned char yaw = blockState.rotation & 3;
        for ( int ax = 0; ax < size_x; ax++ ) {
            for ( int ay = 0; ay < size_y; ay++ ) {
                for ( int az = 0; az < size_z; az++ ) {
                    BlockCoords *arm = alloc( );
                    if ( arm == nullptr ) {
                        continue;
                    }
                    *arm = *blockCoord;
                    arm->x = blockCoord->x + ax;
                    arm->y = blockCoord->y + ay;
                    arm->z = blockCoord->z + az;
                    arm->mesh_x = 1;
                    arm->mesh_y = 1;
                    arm->mesh_z = 1;
                    // Shaft quads are all interior to
                    // the cell: a boundary-covering
                    // neighbor can't hide a lateral
                    // while the adjoining cell's
                    // throat stays open, so force
                    // every face lit before the cap
                    // skips. A fully sealed cavity
                    // just makes them overdraw.
                    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                        arm->packed_lighting[ f ] = NO_LIGHT_BRIGHT;
                    }
                    if ( head_face == FACE_TOP || head_face == FACE_BOTTOM ) {
                        arm->scale_x = 4;
                        arm->scale_y = 12;
                        arm->scale_z = 4;
                        arm->offset_x = 6;
                        arm->offset_y = head_face == FACE_TOP ? 0 : 4;
                        arm->offset_z = 6;
                        arm->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
                        arm->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
                    } else {
                        arm->scale_x = 4;
                        arm->scale_y = 4;
                        arm->scale_z = 12;
                        arm->offset_x = 6;
                        arm->offset_y = 6;
                        arm->offset_z = 0;
                        // Definition-space end caps: land
                        // them on their world-space
                        // packed_lighting slots like the
                        // multi-box skip_faces remap.
                        arm->packed_lighting[ mesh_rotated_face( FACE_FRONT, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                        arm->packed_lighting[ mesh_rotated_face( FACE_BACK, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                    }
                    // Wood grain runs along the shaft on
                    // every lateral. In def space the
                    // shaft always lies along its
                    // sampled v — except the horizontal
                    // LEFT/RIGHT slots, whose u runs
                    // along z; those take the rotated
                    // tile.
                    const bool vertical = head_face == FACE_TOP || head_face == FACE_BOTTOM;
                    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                        arm->face[ f ] = !vertical && ( f == FACE_LEFT || f == FACE_RIGHT ) ? TEX_PISTON_ARM_H : TEX_PISTON_ARM;
                    }
                }
            }
        }
    }

    // Extended piston bases emit the other piece of the
    // arm: the 4x4x4 stub inside the body's recessed
    // throat (Java's piston_extended model keeps it on
    // the base). It continues the head's shaft through
    // the open 4px of this cell up to the body face.
    // Same layout as the head arm — def-space for
    // horizontal yaws, world-baked for up/down — and the
    // same arm tiles, so the whole arm reads as one
    // wooden shaft. Both caps are skipped: the head end
    // meets the head arm's skipped cap at the boundary,
    // the body end is coplanar with the recessed face.
    if ( block->is_piston && blockState.current_redstone_power > 0 ) {
        const int head_face = piston_head_face( blockState.rotation );
        const unsigned char yaw = blockState.rotation & 3;
        for ( int ax = 0; ax < size_x; ax++ ) {
            for ( int ay = 0; ay < size_y; ay++ ) {
                for ( int az = 0; az < size_z; az++ ) {
                    BlockCoords *arm = alloc( );
                    if ( arm == nullptr ) {
                        continue;
                    }
                    *arm = *blockCoord;
                    arm->x = blockCoord->x + ax;
                    arm->y = blockCoord->y + ay;
                    arm->z = blockCoord->z + az;
                    arm->mesh_x = 1;
                    arm->mesh_y = 1;
                    arm->mesh_z = 1;
                    // Same interior-face rule as the
                    // head shaft: force laterals lit,
                    // then skip the end caps.
                    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                        arm->packed_lighting[ f ] = NO_LIGHT_BRIGHT;
                    }
                    arm->scale_x = 4;
                    arm->scale_y = 4;
                    arm->scale_z = 4;
                    arm->offset_x = 6;
                    arm->offset_z = 6;
                    if ( head_face == FACE_TOP || head_face == FACE_BOTTOM ) {
                        arm->offset_y = head_face == FACE_TOP ? 12 : 0;
                        arm->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
                        arm->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
                    } else {
                        arm->offset_y = 6;
                        arm->offset_z = 12;
                        arm->packed_lighting[ mesh_rotated_face( FACE_FRONT, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                        arm->packed_lighting[ mesh_rotated_face( FACE_BACK, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                    }
                    // Same grain rule as the head arm.
                    const bool vertical = head_face == FACE_TOP || head_face == FACE_BOTTOM;
                    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                        arm->face[ f ] = !vertical && ( f == FACE_LEFT || f == FACE_RIGHT ) ? TEX_PISTON_ARM_H : TEX_PISTON_ARM;
                    }
                }
            }
        }
    }

    // Spine-shell blocks (cactus) replace the core box
    // with Java's three-element model: a full-cell box
    // drawing only top and bottom, plus two 14-wide
    // boxes whose only faces are the side quads sitting
    // 1px inside each cell edge. Every face samples its
    // whole tile, so the side tile's opaque interior
    // forms the inset body while its mostly-transparent
    // edge columns read as spines poking out at the cell
    // boundary; the quads cross at the corners (they're
    // independent planes, not a real cube). The side
    // quads live inside the cell so they never z-fight;
    // top/bottom sit on the boundary and are culled when
    // the neighbor's opposing face fully covers it —
    // the same mesh_face_seethrough test phase 1 uses
    // (Java's cullface), read from the halo cells the
    // fence lookup already uses. Block scale/offset in
    // the JSON describes the pick/collision box only.
    if ( block->spine_shell ) {
        const GPUBlockDef *mesh_defs = mesh_def_table( );
        BlockCoords *qx = alloc( );
        if ( qx != nullptr ) {
            *qx = *blockCoord;
            qx->scale_x = 14;
            qx->scale_y = 16;
            qx->scale_z = 16;
            qx->offset_x = 1;
            qx->offset_y = 0;
            qx->offset_z = 0;
            qx->tex_offset_x = 0;
            qx->tex_offset_y = 0;
            qx->tex_offset_z = 0;
            qx->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
            qx->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
            qx->packed_lighting[ FACE_FRONT ] = NO_LIGHT_NO_DRAW;
            qx->packed_lighting[ FACE_BACK ] = NO_LIGHT_NO_DRAW;
        }

        BlockCoords *qz = alloc( );
        if ( qz != nullptr ) {
            *qz = *blockCoord;
            qz->scale_x = 16;
            qz->scale_y = 16;
            qz->scale_z = 14;
            qz->offset_x = 0;
            qz->offset_y = 0;
            qz->offset_z = 1;
            qz->tex_offset_x = 0;
            qz->tex_offset_y = 0;
            qz->tex_offset_z = 0;
            qz->packed_lighting[ FACE_TOP ] = NO_LIGHT_NO_DRAW;
            qz->packed_lighting[ FACE_BOTTOM ] = NO_LIGHT_NO_DRAW;
            qz->packed_lighting[ FACE_LEFT ] = NO_LIGHT_NO_DRAW;
            qz->packed_lighting[ FACE_RIGHT ] = NO_LIGHT_NO_DRAW;
        }

        blockCoord->scale_x = 16;
        blockCoord->scale_y = 16;
        blockCoord->scale_z = 16;
        blockCoord->offset_x = 0;
        blockCoord->offset_y = 0;
        blockCoord->offset_z = 0;
        blockCoord->tex_offset_x = 0;
        blockCoord->tex_offset_y = 0;
        blockCoord->tex_offset_z = 0;
        blockCoord->packed_lighting[ FACE_RIGHT ] = NO_LIGHT_NO_DRAW;
        blockCoord->packed_lighting[ FACE_FRONT ] = NO_LIGHT_NO_DRAW;
        blockCoord->packed_lighting[ FACE_LEFT ] = NO_LIGHT_NO_DRAW;
        blockCoord->packed_lighting[ FACE_BACK ] = NO_LIGHT_NO_DRAW;
        if ( p.cull_against_neighbors ) {
            for ( int f = FACE_TOP; f <= FACE_BOTTOM; f++ ) {
                const BlockState &ncell = p.nb.at( 0, FACE_DIR_Y_OFFSETS[ f ], 0 );
                const int nrot = ncell.rotation;
                const int opposing_face = OPPOSITE_FACE[ mesh_rotated_face( f, nrot ) ];
                if ( !mesh_face_seethrough( mesh_defs[ ncell.id ], ncell.current_redstone_power != 0, nrot, opposing_face ) ) {
                    blockCoord->packed_lighting[ f ] = NO_LIGHT_NO_DRAW;
                }
            }
        }
    }

    // Multi-box models (fences, gates): box 0 overwrites
    // the base instance; extra boxes copy it wholesale so
    // they inherit its packed_lighting, then get their own
    // scale/offset, tex_offset = -offset (the torch
    // convention: texture position follows world
    // position), and NO_LIGHT_NO_DRAW on skip_faces —
    // rail ends buried in the post or a connected cell.
    if ( num_boxes > 0 ) {
        const unsigned char yaw = block_yaw( block, blockState.rotation );
        for ( int i = 0; i < num_boxes; i++ ) {
            BlockCoords *boxCoord = blockCoord;
            if ( i > 0 ) {
                boxCoord = alloc( );
                if ( boxCoord == nullptr ) {
                    break;
                }
                *boxCoord = *blockCoord;
            }
            boxCoord->scale_x = boxes[ i ].scale[ 0 ];
            boxCoord->scale_y = boxes[ i ].scale[ 1 ];
            boxCoord->scale_z = boxes[ i ].scale[ 2 ];
            boxCoord->offset_x = boxes[ i ].offset[ 0 ];
            boxCoord->offset_y = boxes[ i ].offset[ 1 ];
            boxCoord->offset_z = boxes[ i ].offset[ 2 ];
            boxCoord->tex_offset_x = -boxes[ i ].offset[ 0 ];
            boxCoord->tex_offset_y = -boxes[ i ].offset[ 1 ];
            boxCoord->tex_offset_z = -boxes[ i ].offset[ 2 ];
            // The sign board sits at z 7-8 but its
            // front/back faces span the full cell width —
            // the depth-anchored window [7,23) would run
            // off the tile, so pull it back to the
            // full-width band at cols 0-15. The plate's
            // top/bottom quads then sample rows 0-1 of
            // the cap tile (planks cover them).
            if ( ( block->is_standing_sign || block->is_hanging_sign ) && i == 0 ) {
                boxCoord->tex_offset_z = 0;
            }
            boxCoord->face_shift = yaw;
            // skip_faces is definition-space while
            // packed_lighting indexes the world-space quad
            // (faceType) the shader turns the box onto —
            // a yawed box needs its skip bits rotated the
            // same way. mesh_rotated_face maps world->def,
            // so the def->world slot is its counter-yaw row
            // (TOP/BOTTOM map to themselves at every yaw).
            for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                if ( boxes[ i ].skip_faces & ( 1u << f ) ) {
                    boxCoord->packed_lighting[ mesh_rotated_face( f, ( 4 - yaw ) & 3 ) ] = NO_LIGHT_NO_DRAW;
                }
            }
        }
    }
    return emitted;
}
