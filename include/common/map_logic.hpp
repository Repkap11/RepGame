// Included inside the per-y loop of every map_gen backend (C, CUDA, HIP).
// In scope at the include site:
//   x, y, z      - world block coords (ints)
//   col          - const MapGenColumn for this (x, z), computed once per column
//   index        - destination index into the chunk's block array
// Produces finalBlockId, which the includer stores.
const BlockID finalBlockId = mg_pick_block( x, y, z, col );
