// mesh_vis_logic.hpp — phase-1 per-cell mesh logic in the wgsl_dialect.hpp
// dialect, included inside the per-cell loop of every meshing backend (CPU
// in chunk.cpp, HIP in mesh_gen.hip, WebGPU in mesh_gen.wgsl) the same way
// map_logic.hpp is included inside the mapgen kernels.
//
// In scope at the include site (each backend provides):
//   index, x, y, z        - internal (halo-padded) cell index and 0-based
//                           chunk block coords, all ints
//   block_defs            - the GPUBlockDef table (C++/HIP: const
//                           GPUBlockDef*; WGSL: storage array)
//   MESH_CELL_AT( i )     - reads the cell word at internal index i as
//                           unsigned/u32 (WGSL unpacks u16 pairs)
//   MESH_OUT_FLAGS( index, can_be_seen, visible )
//                         - writes the flags (WGSL packs them into the
//                           bool-layout bytes of the result record)
//   MESH_OUT_LIGHT( index, face, u32 )
//                         - writes packed_lighting[face]
//   mesh_gen_fns.hpp helpers (mesh_cell_*, mesh_*_face, ...) must already
//   be declared — the includer includes it before the loop.
//
// Produces (via the MESH_OUT_* macros) the flags word and all six face
// light values — bit-exact WorkingSpace content for the cell.

MG_LET( unsigned, mesh_cell_v, MESH_CELL_AT( index ) )
MG_LET( unsigned, block_id, mesh_cell_id( mesh_cell_v ) )
MG_LET( GPUBlockDef, block, block_defs[ block_id ] )

// render_order_is_visible: renderOrder != RenderOrder_Transparent
MG_LET( int, visible, MG_TERN( block.renderOrder != RenderOrder_Transparent, 1, 0 ) )
MG_VAR( int, can_be_seen, 0 )
// Bit f of vis_mask = the CPU loop's visible_from[f]: face f's surface is
// visible through/at the neighbor in direction f.
MG_VAR( int, vis_mask, 0 )
// The face-light value for visible faces; stays 0 when the cell is
// invisible so hidden cells read back as the memset zero the CPU leaves.
MG_VAR( unsigned, lit, 0u )

if ( visible != 0 ) {
    MG_LET( int, block_rotation, mesh_cell_rotation( mesh_cell_v ) )
    MG_FOR( face, FACE_TOP, face < NUM_FACES_IN_CUBE, face++ ) {
        MG_LET( int, rotated_face, mesh_rotated_face( face, block_rotation ) )
        MG_LET( int, next_to_index,
                mesh_index_from_coords( x + FACE_DIR_X_OFFSETS[ face ], y + FACE_DIR_Y_OFFSETS[ face ], z + FACE_DIR_Z_OFFSETS[ face ] ) )
        MG_LET( unsigned, next_to_cell, MESH_CELL_AT( next_to_index ) )
        MG_LET( unsigned, next_to_id, mesh_cell_id( next_to_cell ) )
        MG_LET( int, next_to_rotation, mesh_cell_rotation( next_to_cell ) )
        MG_LET( int, next_to_power, mesh_cell_powered( next_to_cell ) )
        MG_LET( GPUBlockDef, block_next_to, block_defs[ next_to_id ] )
        MG_LET( int, opposing_face, OPPOSITE_FACE[ mesh_rotated_face( face, next_to_rotation ) ] )

        MG_VAR( int, face_vis, 0 )
        if ( block.is_seethrough_face[ rotated_face ] != 0u && block.hides_self[ rotated_face ] != 0u ) { // water and glass
            // Waterlogged cells count as water: water (and lava) draws no
            // surface faces against them.
            MG_LET( int, liquid_boundary,
                    MG_TERN( block.renderOrder == RenderOrder_Water && mesh_cell_waterlogged( next_to_cell ) != 0, 1, 0 ) )
            if ( face == FACE_TOP && block.renderOrder == RenderOrder_Water ) {
                face_vis = MG_TERN( block_next_to.id != block.id && liquid_boundary == 0, 1, 0 );
            } else {
                face_vis = MG_TERN( mesh_face_seethrough( block_next_to, next_to_power, next_to_rotation, opposing_face ) != 0 &&
                                        next_to_id != block_id && liquid_boundary == 0,
                                    1, 0 );
            }
        } else if ( block.is_seethrough_face[ rotated_face ] != 0u ) { // The current block is seethrough on this face — check all directions not opposing it.
            MG_VAR( int, through_opposing, 0 )
            MG_FOR( face_around, FACE_TOP, face_around < NUM_FACES_IN_CUBE, face_around++ ) {
                if ( face_around == opposing_face ) {
                    // It's the non_opposing_face and parallel with this face — don't count it.
                    continue;
                }
                MG_LET( int, around_index, mesh_index_from_coords( x + FACE_DIR_X_OFFSETS[ face_around ], y + FACE_DIR_Y_OFFSETS[ face_around ],
                                                                 z + FACE_DIR_Z_OFFSETS[ face_around ] ) )
                MG_LET( unsigned, around_cell, MESH_CELL_AT( around_index ) )
                MG_LET( int, around_rotation, mesh_cell_rotation( around_cell ) )
                MG_LET( int, around_power, mesh_cell_powered( around_cell ) )
                MG_LET( GPUBlockDef, block_around, block_defs[ mesh_cell_id( around_cell ) ] )
                MG_LET( int, around_face, OPPOSITE_FACE[ mesh_rotated_face( face_around, around_rotation ) ] )
                if ( mesh_face_seethrough( block_around, around_power, around_rotation, around_face ) != 0 ) {
                    through_opposing = 1;
                    break;
                }
            }
            face_vis = through_opposing;
        } else {
            face_vis = mesh_face_seethrough( block_next_to, next_to_power, next_to_rotation, opposing_face );
        }
        vis_mask |= MG_SHL( face_vis, face );
    }
    can_be_seen = MG_TERN( vis_mask != 0, 1, 0 );
    // Per-vertex corner AO removed: the flood-fill light volume already
    // darkens crevices, so visible faces are packed as fully lit
    // (NO_LIGHT_BRIGHT). Hidden faces keep the block's no_light sentinel
    // (NO_LIGHT_NO_DRAW clips the quad in the shader; leaf interiors use
    // NO_LIGHT_DRAW). render_order_can_be_shaded: Opaque or Translucent.
    lit = MG_TERN( block.renderOrder == RenderOrder_Opaque || block.renderOrder == RenderOrder_Translucent, NO_LIGHT_BRIGHT, block.no_light );
}

MESH_OUT_FLAGS( index, can_be_seen, visible );
MG_FOR( i, FACE_TOP, i < NUM_FACES_IN_CUBE, i++ ) {
    MG_VAR( unsigned, face_light, 0u )
    if ( visible != 0 ) {
        face_light = MG_TERN( ( vis_mask & MG_SHL( 1, i ) ) != 0, lit, block.no_light );
    }
    MESH_OUT_LIGHT( index, i, face_light );
}
