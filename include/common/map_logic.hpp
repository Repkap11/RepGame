// Included inside the per-y loop of every map_gen backend (C, CUDA, HIP).
// In scope at the include site:
//   x, y, z      - world block coords (ints)
//   index        - destination index into the chunk's block array
//   plus, per generator:
//     new    - col: const MapGenColumn for this (x, z), computed per column
//     legacy - terrainHeight: float terrain height for this (x, z)
// Produces finalBlockId, which the includer stores.
#if defined( REPGAME_MAP_GEN_LEGACY )
const BlockID finalBlockId = mgl_pick_block( x, y, z, terrainHeight );
#else
const BlockID finalBlockId = mg_pick_block( x, y, z, col );
#endif
