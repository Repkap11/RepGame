#pragma once

#include "common/chunk.hpp"

struct SgHeightGrid;

class StructureGen {

    static void place_trees( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z );
    static void place_features( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z );
    static void place_decorations( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z, SgHeightGrid &hg );
    static void place_formations( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z, SgHeightGrid &hg );
    static void place_nether_trees( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z );
    static void place_nether_fortress( Chunk &chunk, int chunk_offset_x, int chunk_offset_y, int chunk_offset_z );
    static void place_legacy( Chunk &chunk );

  public:
    static void place( Chunk &chunk );
    // Generated contents for fortress chests, rolled from world position.
    // Called from Chunk::load_terrain after the chest sidecar load; sidecar
    // entries always win. No-op outside DIM_NETHER.
    static void place_fortress_loot( Chunk &chunk );
    // Test/debug hook: does fortress grid cell (cx, cz) hold one? Outputs
    // the anchor world x/z and the deck's world y when non-null.
    static int fortress_cell_exists( int cx, int cz, glm::ivec3 *anchor, int *deck_y );
};
