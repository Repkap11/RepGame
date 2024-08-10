#pragma once

#include "common/chunk.hpp"

class StructureGen {

    static int is_forest( int x, int z );
    static int is_tree_roll( int x, int z, int max_tree_radius );
    static BlockID is_long_grass_roll( int x, int z, int is_forest );
    static int is_reed_roll( int x, int z );
    static int poll_fits( int x, int y, int z, int height );
    static int tree_fits( Chunk &chunk, int x, int y, int z, int max_tree_radius );
    static int is_next_to( Chunk &chunk, int x, int y, int z, BlockID block );
    static void place_leaves( Chunk &chunk, int x, int y, int z, int tree_type, BlockState leaf_state );

  public:
    static void place( Chunk &chunk );
};
