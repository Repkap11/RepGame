#pragma once


#include "chunk.hpp"

// World-layout constants, MAP_SEED and MapGenColumn live in
// map_gen_consts.hpp — a pure-preprocessor + dual-spelled header also
// included by the WGSL generation pass. Both terrain generators are always
// compiled: the overworld band runs the legacy generator
// (map_gen_fields_legacy.hpp, the code live at commit 101a348) and the
// repheim band runs the reworked generator (map_gen_fields.hpp: noodly
// caves, overhangs, biomes, lava layer, ...). The rest of the world
// (structures, waterlogging, meshing) is unchanged either way.
#include "common/map_gen_consts.hpp"
#include "common/dimension.hpp"

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
    // Column surface height in a dimension's own generator frame: the
    // legacy heightmap for the overworld band, the reworked mg_base_height
    // for repheim. Used by the empty-chunk fast path and tests.
    static float dimTerrainHeight( DimensionId dim, int x, int z );
    // Theoretical maximum of a dimension's terrain height + overhang rise,
    // derived from component formulas: the legacy bound for the overworld
    // band, the reworked bound for repheim (rep frame shares the overworld's
    // y range). Used for O(1) empty-chunk detection.
    static float maxTerrainHeight( DimensionId dim );
    // CPU evaluation of a single generated block (terrain only, no structures).
    // Used by structure gen for border-crossing ground checks and by spawn
    // placement; identical formulas to all backends by construction.
    static BlockID gen_block_id( int x, int y, int z );
    // Block-world debug layout (REPGAME_BLOCK_WORLD): sandstone floor at
    // y=0, every defined block on a 32-wide grid standing on it at y=1 —
    // static blocks at x>=2, animated blocks on a mirrored grid at x<=-8
    // (clear of the mob showcase column at x=-3).
    // The z<0 showcase cells may carry rotation (wall-mounted blocks);
    // block_world_id unwraps the id for callers that don't need it.
    // The mob world (REPGAME_MOB_WORLD) swaps in mob_stress_arena_cell.
    // O(1) per cell; used by Chunk::load_terrain and gen_block_id.
    static BlockCell block_world_cell( int x, int y, int z );
    static BlockID block_world_id( int x, int y, int z );
    // Flat-world generator (world_type `flat`, set via
    // MapStorage::detect_flat_world): solid sandstone at y<=0, air above.
    // Saved chunks still load from disk — this only fills ungenerated cells.
    static BlockID flat_world_id( int x, int y, int z );
};
void map_gen_load_block_cuda( glm::ivec3 *chunk_pos, BlockCell *cells );
bool map_gen_load_block_hip( glm::ivec3 *chunk_pos, BlockCell *cells );
#if defined( REPGAME_WASM )
// Per-frame pump for the WebGPU backend — all wgpu calls run on the browser
// main thread because async callbacks resolve through its JS event loop.
// Called once per frame from main_loop_wasm (src/wasm/RepGameSDL2.cpp).
void webgpu_mapgen_pump( );
// Releases every worker parked on a mapgen job before cleanup() joins the
// terrain threads. Called once from main_loop_wasm's exit path — after this
// the pump never runs again and in-flight mapAsync callbacks may never fire.
void webgpu_mapgen_shutdown( );
#endif
