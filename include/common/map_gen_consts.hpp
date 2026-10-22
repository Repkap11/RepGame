#pragma once

// Mapgen constants + the per-column context struct, split out of map_gen.hpp
// and constants.hpp so this header is safe to include from C++, CUDA/HIP,
// AND the WGSL generation pass (gcc -E -DREPGAME_WGSL_GEN): in WGSL mode
// every #define expands silently and only the WGSL-spelled declarations
// below are emitted into the shader source. Any value here changes terrain
// for every backend at once — the whole point of the single-source dialect.

// Compile-time terrain generator selection, shared by all platforms and all
// map_gen backends (CPU, CUDA, HIP, WebGPU). 1 builds the pre-overhaul
// generator (map_gen_fields_legacy.hpp, the code live at commit 101a348);
// 0 builds the reworked generator (noodly caves, overhangs, biomes, lava
// layer) in map_gen_fields.hpp. The rest of the world (structures,
// waterlogging, meshing) is unchanged either way. #ifndef so a -D override
// can A/B the generators without editing the tree.
#ifndef REPGAME_MAP_GEN_LEGACY
#define REPGAME_MAP_GEN_LEGACY 1
#endif

#define MAP_SEED 0

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

// Per-column context computed once per (x, z) by each backend kernel before
// the per-y loop; consumed by the shared block picker in map_gen_fields.hpp.
#if defined( REPGAME_WGSL_GEN )
struct MapGenColumn {
    height : f32,      // 2D base heightmap at this column
    biome : f32,       // temperature-like field [0, 1]
    weird : f32,       // secondary biome field [0, 1]
    steepness : f32,   // max |height difference| to x+1/z+1 neighbors
    rough_gain : f32,  // overhang noise amplitude for this column
    island : f32,      // floating-island strength [0, 1]; 0 = no islands here
    island_dy0 : f32,  // island band center above the heightmap (blocks)
    mesa_riser : f32,  // 1 on the cliff bands of a terraced mesa, else 0
};
#else
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
#endif
