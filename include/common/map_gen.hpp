#pragma once


#include "chunk.hpp"

// World-layout constants, REPGAME_MAP_GEN_LEGACY, MAP_SEED and MapGenColumn
// live in map_gen_consts.hpp — a pure-preprocessor + dual-spelled header also
// included by the WGSL generation pass. REPGAME_MAP_GEN_LEGACY=1 selects the
// pre-overhaul generator (the code live at commit 101a348, fields in
// map_gen_fields_legacy.hpp); 0 selects the reworked generator (shared
// CPU/CUDA/HIP/WebGPU fields in map_gen_fields.hpp: noodly caves, overhangs,
// biomes, lava layer, ...). The rest of the world (structures, waterlogging,
// meshing) is unchanged either way.
#include "common/map_gen_consts.hpp"

// Tree placement: the world is divided into cells of this size; each cell
// rolls at most one tree. Also bounds the canopy reach a tree may have.
// 5 puts dense-forest trees close enough that canopies merge into cover.
#define MAPGEN_TREE_CELL 5
#define MAPGEN_MAX_CANOPY_RADIUS 8
// Highest a tree part can reach above the ground it grows on (giant jungle
// trunk + canopy headroom). Empty-chunk detection must include it above the
// max possible ground height or treetops on extreme peaks would clip.
#define MAPGEN_MAX_TREE_REACH 34

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
    // WebGPU backend (WASM builds only — src/wasm/webgpu_mapgen.cpp). Follows
    // the HIP contract: load_block_webgpu returns false on failure so the
    // caller can fall back to load_block_c, and supports_webgpu goes true
    // once the async adapter/device/pipeline chain lands.
    static bool load_block_webgpu( Chunk *chunk );
    static int supports_webgpu( );
    static float calculateTerrainHeight( int x, int z );
    // Theoretical maximum of calculateTerrainHeight + overhang rise, derived
    // from component formulas. Used for O(1) empty-chunk detection.
    static float maxTerrainHeight( );
    // CPU evaluation of a single generated block (terrain only, no structures).
    // Used by structure gen for border-crossing ground checks and by spawn
    // placement; identical formulas to all backends by construction.
    static BlockID gen_block_id( int x, int y, int z );
};
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockCell *cells );
bool map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockCell *cells );
#if defined( REPGAME_WASM )
// Per-frame pump for the WebGPU backend — all wgpu calls run on the browser
// main thread because async callbacks resolve through its JS event loop.
// Called once per frame from main_loop_wasm (src/wasm/RepGameSDL2.cpp).
void webgpu_mapgen_pump( );
#endif
#if defined( REPGAME_WASM )
// Per-frame pump for the WebGPU backend — all wgpu calls run on the browser
// main thread because async callbacks resolve through its JS event loop.
// Called once per frame from main_loop_wasm (src/wasm/RepGameSDL2.cpp).
void webgpu_mapgen_pump( );
#endif
