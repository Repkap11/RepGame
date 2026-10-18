#pragma once


#include "chunk.hpp"

// Compile-time terrain generator switch: REPGAME_MAP_GEN_LEGACY in
// constants.hpp selects the pre-overhaul generator (the code live at commit
// 101a348, fields in map_gen_fields_legacy.hpp); 0 selects the reworked
// generator (shared CPU/CUDA/HIP fields in map_gen_fields.hpp: noodly caves,
// overhangs, biomes, lava layer, ...). The rest of the world (structures,
// waterlogging, meshing) is unchanged either way.

// ---- World layout ----------------------------------------------------------
#define WATER_LEVEL 0        // Sea level: air cells below this become WATER.
#define BEDROCK_LEVEL -100   // Top of the (jittered) bedrock floor.
#define LAVA_LEVEL -80       // Carved cave space at/below this becomes LAVA.
#define SNOW_LINE 60         // Exposed surface above this gets snow.
#define MOUNTAIN_ROCK_LINE 30 // Above this, surface tends to bare stone.
#define MAX_MOUNTAIN_HEIGHT 200.0f

// Terraced mesa plateaus added on top of desert columns (map_gen_fields.hpp):
// the quantizer can round the raw mesa term up by one full step.
#define MESA_MAX_RISE 30.0f

// Deepest a slot canyon can cut (map_gen_fields.hpp). Carving only lowers
// the heightmap, so no empty-chunk bound is needed for it.
#define CANYON_MAX_DEPTH 46.0f

// Overhang 3D noise and floating islands are gated to a y-band above the
// heightmap; solid terrain can never appear higher than
// terrainHeight + OVERHANG_MAX_RISE. chunk.cpp's empty-chunk early-out relies
// on this bound being correct.
#define OVERHANG_MAX_RISE 60.0f

// Thickness of the biome surface layer (grass/dirt, sand, snow...) under the
// top solid block of a column.
#define SURFACE_BAND 4.0f

// Tree placement: the world is divided into cells of this size; each cell
// rolls at most one tree. Also bounds the canopy reach a tree may have.
// 5 puts dense-forest trees close enough that canopies merge into cover.
#define MAPGEN_TREE_CELL 5
#define MAPGEN_MAX_CANOPY_RADIUS 8
// Highest a tree part can reach above the ground it grows on (giant jungle
// trunk + canopy headroom). Empty-chunk detection must include it above the
// max possible ground height or treetops on extreme peaks would clip.
#define MAPGEN_MAX_TREE_REACH 34

// Per-column context computed once per (x, z) by each backend kernel before
// the per-y loop; consumed by the shared block picker in map_gen_fields.hpp.
typedef struct {
    float height;      // 2D base heightmap at this column
    float biome;       // temperature-like field [0, 1]
    float weird;       // secondary biome field [0, 1]
    float steepness;   // max |height difference| to x+1/z+1 neighbors
    float rough_gain;  // overhang noise amplitude for this column
    float island;      // floating-island strength [0, 1]; 0 = no islands here
    float island_dy0;  // island band center above the heightmap (blocks)
    float mesa_riser;  // 1 on the cliff bands of a terraced mesa, else 0
} MapGenColumn;

class MapGen {
  public:
    static void load_block_cuda( Chunk *chunk );
    static bool load_block_hip( Chunk *chunk );
    static void load_block_c( const Chunk *chunk );
    static void free_block( Chunk *chunk );
    static int supports_cuda( );
    static int host_supports_cuda( );
    static int supports_hip( );
    static int host_supports_hip( );
    static float calculateTerrainHeight( int x, int z );
    // Theoretical maximum of calculateTerrainHeight + overhang rise, derived
    // from component formulas. Used for O(1) empty-chunk detection.
    static float maxTerrainHeight( );
    // CPU evaluation of a single generated block (terrain only, no structures).
    // Used by structure gen for border-crossing ground checks and by spawn
    // placement; identical formulas to all backends by construction.
    static BlockID gen_block_id( int x, int y, int z );
};
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockState *blocks );
bool map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockState *blocks );
