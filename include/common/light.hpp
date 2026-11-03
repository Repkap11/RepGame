#pragma once

#include <glm.hpp>
#include "common/constants.hpp"
#include "common/block_definitions.hpp" // BlockID for light_air_mask_set

// Flood-fill lighting. Each chunk keeps a light[] volume matching the
// internal 34^3 block layout: each u16 cell packs one byte per channel —
// skylight in the high byte, block (emissive) light in the low byte. Channel
// values are raw units: LIGHT_RAW_PER_LEVEL per display level, so 255 =
// level 15. The values are mirrored into a 3D texture per chunk so the
// fragment shader can sample light per face-adjacent air cell;
// nothing here touches the mesh, so light updates never cause a remesh.
//
// Light mutation is single-writer: on native builds a dedicated lighting
// thread runs finalize/propagation (docs/lighting-thread-plan.md); without it
// (WASM without pthreads, tests) the same methods run inline on the calling
// thread. Workers only do the initial fill in load_terrain.
#if defined( REPGAME_WASM ) && !defined( __EMSCRIPTEN_PTHREADS__ )
#define LIGHT_ON_THREAD 0
#else
#define LIGHT_ON_THREAD 1
#endif

#define LIGHT_CHANNEL_BLOCK 0
#define LIGHT_CHANNEL_SKY 1

#define LIGHT_MAX_LEVEL 15

// Raw channel units: 17 per display level fills the byte exactly
// (15*17 = 255). Propagation is a chamfer-weighted flood — face steps cost
// one level (17), edge diagonals ~sqrt(2) (24), corner diagonals ~sqrt(3)
// (29) — so fractional distances survive in the raw value and iso-contours
// are near-circular instead of Manhattan diamonds.
#define LIGHT_RAW_PER_LEVEL 17
#define LIGHT_RAW_MAX ( LIGHT_MAX_LEVEL * LIGHT_RAW_PER_LEVEL )
#define LIGHT_STEP_FACE 17
#define LIGHT_STEP_EDGE 24
#define LIGHT_STEP_CORNER 29

// Column flags live in chunk->light_columns: [0, flags_count) is column_open
// (no light-blocking cell in this chunk's vertical column),
// [flags_count, 2*flags_count) is sky_open_above (no blocker anywhere above
// inside the loaded grid), and [2*flags_count, 3*flags_count) is fill_from —
// the first y skylight reaches (one past the topmost opaque cell; 0 for a
// fully-open column), so the cascade fill needs no per-cell opacity reads.
// Indexed by interior (x,z) in [0,CHUNK_SIZE).
#define LIGHT_FLAGS_COUNT ( CHUNK_SIZE_X * CHUNK_SIZE_Z )
#define LIGHT_COLUMN_INDEX( x, z ) ( (x)*CHUNK_SIZE_Z + ( z ) )
#define LIGHT_FILL_FROM_INDEX( x, z ) ( 2 * LIGHT_FLAGS_COUNT + LIGHT_COLUMN_INDEX( x, z ) )

// Index into Chunk::light[] for internal coords x,y,z in [-1, CHUNK_SIZE].
// X-fastest layout so a dirty sub-box can be uploaded to the 3D texture
// directly with GL_UNPACK_ROW_LENGTH / GL_UNPACK_IMAGE_HEIGHT.
static inline int light_index( int x, int y, int z ) {
    return ( ( z + 1 ) * CHUNK_SIZE_INTERNAL_Y + ( y + 1 ) ) * CHUNK_SIZE_INTERNAL_X + ( x + 1 );
}

static inline int light_get_block( unsigned short packed ) {
    return packed & 0xFF;
}

static inline int light_get_sky( unsigned short packed ) {
    return ( packed >> 8 ) & 0xFF;
}

static inline unsigned short light_pack( const int sky, const int block ) {
    return static_cast<unsigned short>( ( sky << 8 ) | block );
}

// Day/night: one real-ish day is DAY_LENGTH_TICKS ticks of RepGame::tick.
// world_time == 0 is dawn (sunrise); DAY_LENGTH_TICKS/4 is noon.
#define DAY_LENGTH_TICKS ( 20 * 60 * UPS_RATE )

// Returns 0.04 (deep night) to 1.0 (full day) for a world_time tick count.
float light_daylight_factor( long world_time );

// One shared texture unit for all per-chunk light volumes: each chunk's 3D
// texture is bound to this unit right before its draw call.
int light_texture_unit( );

// Whether per-chunk light volumes use the mipmapped RGBA mode (lit-cell
// fraction in B, footprint-LOD sampling in the chunk shader) or the flat
// RG8 mode. Constant for the process lifetime — texture formats depend on
// it. Default: off on Android/WASM, on elsewhere; REPGAME_LIGHT_MIPMAPS=0|1
// overrides on every platform.
bool light_mipmaps_enabled( );

// The "Shadows" pause-menu setting: whether the uploaded light volume gets
// the air-gated 3x3x3 smoothing blur (SMOOTH) or ships the raw chamfer field
// (FAST). Runtime-toggleable — toggling re-uploads every live texture via
// ChunkLoader::light_shadow_refresh_all. REPGAME_LIGHT_SMOOTH, when set,
// pins the mode and ignores the menu (keeps captures/A-B runs
// deterministic). light_shadow_smooth is read by worker and render
// threads.
bool light_shadow_smooth( );              // effective mode right now
void light_shadow_smooth_set( bool on );  // user preference (no-op when forced)
bool light_shadow_smooth_forced( );       // env var pinned the mode

class Chunk;
// Initial per-chunk light fill, run on the terrain worker from
// Chunk::load_terrain: zeroes the volume, flood-fills emitters (block channel
// only), computes column_open/fill_from, and prefills an optimistic skylight
// assumption. Skylight is verified later on the lighting thread by
// ChunkLoader::light_finalize_chunk.
void light_fill_chunk( Chunk &chunk );

// Stamps one bit of chunk.light_air_mask (the air-mask cache the light
// texture upload reads instead of walking cells[]) for a post-fill cells[]
// write — Chunk::set_block and Chunk::set_block_by_index_if_different call
// this after every real write. Bit set = !light_opaque_id(id), the same
// predicate the fill/upload use. Also stamps the matching
// chunk.light_shade_mask bit (set = def->blocks_light_down). No-op when a
// mask isn't allocated, so a missing mask always means "upload falls back
// to the cells[] walk".
void light_air_mask_set( const Chunk &chunk, int index, BlockID id );
