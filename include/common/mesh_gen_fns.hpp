#pragma once

// ---------------------------------------------------------------------------
// mesh_gen_fns.hpp — dialect helper functions for phase-1 meshing, shared by
// the CPU loop (chunk.cpp), the HIP kernel (mesh_gen.hip), and the WGSL
// WebGPU kernel (mesh_gen.wgsl). Requires the tables/GPUBlockDef from
// mesh_gen_shared.hpp (included here).
//
// The includer must define MG_QUAL ("static inline" on CPU, "__device__
// static" on HIP) unless preprocessing with REPGAME_WGSL_GEN.
// ---------------------------------------------------------------------------

#if !defined( MG_QUAL ) && !defined( REPGAME_WGSL_GEN )
#error "mesh_gen_fns.hpp requires MG_QUAL (or REPGAME_WGSL_GEN)"
#endif

#include "common/mesh_gen_shared.hpp"

// Cell words are treated as unsigned/u32 on every backend so the
// BLOCK_CELL_* masks (u32 literals) work identically in WGSL.

MG_FN1( unsigned, mesh_cell_id, unsigned, cell ) {
    return cell & BLOCK_CELL_ID_MASK;
}

MG_FN1( int, mesh_cell_rotation, unsigned, cell ) {
    return MG_I32( MG_SHR( cell & BLOCK_CELL_ROT_MASK, BLOCK_CELL_ROT_SHIFT ) );
}

MG_FN1( int, mesh_cell_powered, unsigned, cell ) {
    return MG_TERN( ( cell & BLOCK_CELL_POWERED ) != 0u, 1, 0 );
}

MG_FN1( int, mesh_cell_waterlogged, unsigned, cell ) {
    return MG_TERN( ( cell & BLOCK_CELL_WATERLOGGED ) != 0u, 1, 0 );
}

MG_FN3( int, mesh_index_from_coords, int, x, int, y, int, z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( x + 1 ) * CHUNK_SIZE_INTERNAL_Z + ( z + 1 );
}

MG_FN2( int, mesh_rotated_face, int, face, int, rotation ) {
    return ROTATED_FACE_TABLE[ rotation & 3 ][ face ];
}

MG_FN1( int, mesh_piston_covered_face, int, rotation ) {
    return MESH_PISTON_COVERED[ MG_TERN( rotation < 6, rotation, 0 ) ];
}

// Mirror of chunk.cpp's face_is_seethrough: a powered piston base recesses
// 4px at its head end, so every def-space face except the covered back face
// lets neighbor faces show through. Only used for covering-block lookups.
MG_FN4( int, mesh_face_seethrough, GPUBlockDef, block, int, power, int, rotation, int, face ) {
    if ( block.is_piston != 0u && power > 0 ) {
        return MG_TERN( face != mesh_piston_covered_face( rotation ), 1, 0 );
    }
    return MG_I32( block.is_seethrough_face[ face ] );
}
