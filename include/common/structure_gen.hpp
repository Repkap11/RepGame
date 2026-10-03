#pragma once

#include "common/chunk.hpp"

class StructureGen {

    static void place_trees( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z );
    static void place_decorations( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z );

  public:
    static void place( Chunk &chunk );
};
