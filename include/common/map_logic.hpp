// Included inside the per-y loop of every map_gen backend (C, CUDA, HIP,
// and the WGSL WebGPU kernel). In scope at the include site:
//   x, y, z      - world block coords (ints)
//   index        - destination index into the chunk's block array
//   plus, per generator:
//     new    - col: const MapGenColumn for this (x, z), computed per column
//     legacy - terrainHeight: float terrain height for this (x, z)
// Produces finalBlockId, which the includer stores.
//
// y is the absolute encoded block y, so the dimension dispatch needs no extra
// plumbing: the nether band runs mgn_pick_block, everything else stays on the
// overworld generator (the spare bands fall through to the overworld pick —
// below-bedrock y is bedrock, above-terrain y is air, which doubles as solid
// filler between the active bands).
#if defined( REPGAME_WGSL_GEN )
var finalBlockId : u32;
#else
BlockID finalBlockId;
#endif
if ( DIM_IS_NETHER_BY( y ) ) {
    finalBlockId = mgn_pick_block( x, y, z );
} else {
#if REPGAME_MAP_GEN_LEGACY
    finalBlockId = mgl_pick_block( x, y, z, terrainHeight );
#else
    finalBlockId = mg_pick_block( x, y, z, col );
#endif
}
