// Included inside the per-y loop of every map_gen backend (C, CUDA, HIP,
// and the WGSL WebGPU kernel). In scope at the include site:
//   x, y, z      - world block coords (ints)
//   index        - destination index into the chunk's block array
//   col          - MapGenColumn for this (x, z): valid when the chunk is in
//                  the repheim band, zeroed otherwise
//   terrainHeight - float legacy terrain height for this (x, z): valid for
//                   every non-repheim chunk
// Produces finalBlockId, which the includer stores.
//
// y is the absolute encoded block y, so the dimension dispatch needs no extra
// plumbing: the nether band runs mgn_pick_block, the end band runs
// mge_pick_block, the repheim band runs the reworked generator (mg_pick_block)
// on the rep frame (band-local y mapped to the overworld's [-1984,1983],
// see dimension.hpp), and everything else runs the legacy overworld picker.
// The backend loop computes `col` only for repheim-band chunks and
// `terrainHeight` only for the rest, so each chunk pays for one generator.
#if defined( REPGAME_WGSL_GEN )
var finalBlockId : u32;
#else
BlockID finalBlockId;
#endif
if ( DIM_IS_NETHER_BY( y ) ) {
    finalBlockId = mgn_pick_block( x, y, z );
} else if ( DIM_IS_END_BY( y ) ) {
    finalBlockId = mge_pick_block( x, y, z );
} else if ( DIM_IS_REPHEIM_BY( y ) ) {
#if defined( REPGAME_WGSL_GEN )
    let rep_y = select( y - DIM_BY_MIN_3_LO, y - DIM_BY_MIN_3_HI - 1984, y >= DIM_BY_MIN_3_HI );
    finalBlockId = mg_pick_block( x, rep_y, z, col );
#else
    // Inlined repheim_gen_block_y (dimension.hpp): that helper is host-only.
    const int rep_y = y >= DIM_BY_MIN_3_HI ? y - DIM_BY_MIN_3_HI - 1984 : y - DIM_BY_MIN_3_LO;
    finalBlockId = mg_pick_block( x, rep_y, z, col );
#endif
} else {
    finalBlockId = mgl_pick_block( x, y, z, terrainHeight );
}
