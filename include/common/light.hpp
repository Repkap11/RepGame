#pragma once

#include <glm.hpp>
#include "common/constants.hpp"

// Flood-fill lighting. Each chunk keeps a light[] volume matching the
// internal 34^3 block layout: the high nibble is skylight, the low nibble is
// block (emissive) light. The values are mirrored into a GL_R8 3D texture per
// chunk so the fragment shader can sample light per face-adjacent air cell;
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

static inline int light_get_block( unsigned char packed ) {
    return packed & 0x0F;
}

static inline int light_get_sky( unsigned char packed ) {
    return ( packed >> 4 ) & 0x0F;
}

// Day/night: one real-ish day is DAY_LENGTH_TICKS ticks of RepGame::tick.
// world_time == 0 is dawn (sunrise); DAY_LENGTH_TICKS/4 is noon.
#define DAY_LENGTH_TICKS ( 20 * 60 * UPS_RATE )

// Returns 0.04 (deep night) to 1.0 (full day) for a world_time tick count.
float light_daylight_factor( long world_time );

// One shared texture unit for all per-chunk light volumes: each chunk's 3D
// texture is bound to this unit right before its draw call.
int light_texture_unit( );

class Chunk;
// Initial per-chunk light fill, run on the terrain worker from
// Chunk::load_terrain: zeroes the volume, flood-fills emitters (block channel
// only), computes column_open/fill_from, and prefills an optimistic skylight
// assumption. Skylight is verified later on the lighting thread by
// ChunkLoader::light_finalize_chunk.
void light_fill_chunk( Chunk &chunk );
