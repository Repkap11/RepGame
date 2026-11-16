#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <limits>
#include <math.h>
#include <mutex>
#include <thread>
#include <vector>

#if defined( _MSC_VER ) && !defined( __clang__ )
#include <intrin.h>
#endif

#include "common/Platforms.hpp" // IWYU pragma: keep — GL headers + platform macros
#include "common/Logging.hpp"
#include "common/light.hpp"
#include "common/light_internal.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/block_definitions.hpp"
#include "common/dimension_profile.hpp"
#include "common/renderer/texture.hpp"

// ---------------------------------------------------------------------------
// GPU upload: dirty-box staging, the smoothed presentation fill, staging
// pool, and the light texture uploads themselves. Shared helpers/state live
// in common/light_internal.hpp.
// ---------------------------------------------------------------------------

static unsigned char *light_stage_pool_take( ) {
    {
        std::lock_guard<std::mutex> lock( light_work_mutex );
        if ( light_stage_live >= LIGHT_STAGE_MAX ) {
            return nullptr;
        }
        light_stage_live++;
        if ( light_stage_pool_n > 0 ) {
            return light_stage_pool[ --light_stage_pool_n ];
        }
    }
    return static_cast<unsigned char *>( malloc( LIGHT_STAGE_BUF_SIZE ) );
}

static void light_stage_pool_give( unsigned char *buf ) {
    std::lock_guard<std::mutex> lock( light_work_mutex );
    light_stage_live--;
    light_stage_pool[ light_stage_pool_n++ ] = buf;
}

void light_reset_staging( Chunk *chunk ) {
    // CAS rather than load/store: a worker publish (BUSY -> READY) racing
    // this reset must not be overwritten — that would orphan the buffer it
    // just handed off (live count leaks, pool eventually starves). The CAS
    // re-reads the winner's state so the READY branch below still reclaims
    // the buffer either way.
    unsigned s = chunk->light.stage_state.load( std::memory_order_acquire );
    while ( !chunk->light.stage_state.compare_exchange_weak( s, ( s & ~3u ) + 4u,
                                                             std::memory_order_acq_rel, std::memory_order_relaxed ) ) {
    }
    if ( LIGHT_STAGE_STATUS( s ) == LIGHT_STAGE_READY && chunk->light.stage_buf != nullptr ) {
        light_stage_pool_give( chunk->light.stage_buf );
    }
    chunk->light.stage_buf = nullptr;
    chunk->light.stage_box = 0;
    chunk->light.stage_mode = 0;
}

int light_upload_backlog( ) {
    std::lock_guard<std::mutex> lock( light_work_mutex );
    return static_cast<int>( light_dirty_list_near.size( ) + light_dirty_list_far.size( ) +
                             light_stage_list.size( ) + static_cast<size_t>( light_stage_live ) );
}
// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------

static int s_light_tex_unit = -1;
int light_texture_unit( ) {
    if ( s_light_tex_unit < 0 ) {
        s_light_tex_unit = texture_reserve_unit( );
    }
    return s_light_tex_unit;
}

// light[] packs both channels in one u16 (sky<<8 | block), but GL_LINEAR
// interpolates the raw value before any shader-side unpack — sampling between
// a sky-lit cell and a torch-lit cell produces bright ridges where both
// channels should dip. The texture therefore stores each channel separately
// (R=sky, G=block) so filtering stays independent; expansion happens here on
// upload. Stored values are already raw units (17 per display level, max
// 255), so the upload is a byte shuffle — full 8-bit precision in UNORM8.
// The light-mipmap mode additionally carries B = 255 for cells that can
// hold light (non-opaque per the same light_opaque_id the BFS uses — water
// counts as air since it doesn't cast a shadow), 0 for solid, plus an
// unused alpha (RGBA8 avoids the driver's RGB->RGBA repack on upload). The
// mipmapped volume then recovers the average light over the *air* cells in
// a footprint as rg / b (see chunk_fragment.glsl). It must be a real air
// mask, not a lit-cell mask: solid cells always hold light 0, so "has
// light" conflates solid with merely-dark air — pitch-black air (deep
// caves) has to sit IN the denominator, otherwise rg / b divides the few
// lit cells in a coarse texel up to full brightness. Opacity comes from
// Chunk::light_air_mask (a 32KB bitmap built by light_fill_chunk while
// cells[] is already streamed, restamped on block edits); the per-cell
// cells[] walk remains only as the fallback for chunks whose mask was
// never built.
bool light_mipmaps_enabled( ) {
    // Resolved once and constant for the process lifetime — the texture
    // format and mip chain are decided by it.
    static const bool enabled = []( ) {
        // Default on everywhere; the env var is the escape hatch if a driver
        // makes 3D mipmap generation expensive on the upload path.
        const bool e = env_bool( "REPGAME_LIGHT_MIPMAPS", true );
        pr_debug( "Light mipmaps: %s", e ? "on" : "off" );
        return e;
    }( );
    return enabled;
}

// Scratch is 4 bytes/texel: mip mode writes a uint32_t texel per cell, the
// RG8 mode writes 2 bytes per cell through the unsigned char* view.
static uint32_t s_light_upload_scratch[ CHUNK_BLOCK_SIZE ];

// BlockID -> air mask (255 = can hold light, 0 = opaque) using the same
// light_opaque_id predicate the propagation BFS uses, so the mask agrees
// with what the light values actually reached. Built on first call: it
// needs block definitions initialised, and file-scope static init order
// across TUs is not guaranteed (same pattern as light_mipmaps_enabled).
static const unsigned char *light_air_lut( ) {
    static unsigned char lut[ LAST_BLOCK_ID + 1 ];
    static const bool init = []( ) {
        for ( int i = 0; i <= LAST_BLOCK_ID; i++ ) {
            lut[ i ] = light_opaque_id( static_cast<BlockID>( i ) ) ? 0 : 255;
        }
        return true;
    }( );
    ( void ) init;
    return lut;
}

// BlockID -> shade alpha (0 = light floor / blocks_light_down, 255 =
// normal) for the light texture's A channel — see light_air_lut and the
// chunk_fragment contact-shadow tap.
static const unsigned char *light_shade_lut( ) {
    static unsigned char lut[ LAST_BLOCK_ID + 1 ];
    static const bool init = []( ) {
        for ( int i = 0; i <= LAST_BLOCK_ID; i++ ) {
            lut[ i ] = block_definition_get_definition( static_cast<BlockID>( i ) )->blocks_light_down ? 0 : 255;
        }
        return true;
    }( );
    ( void ) init;
    return lut;
}

static inline void light_expand_cell( const unsigned short packed, unsigned char *out ) {
    out[ 0 ] = static_cast<unsigned char>( packed >> 8 ); // sky
    out[ 1 ] = static_cast<unsigned char>( packed & 0xFF ); // block
}

// packed light u16 (sky<<8|block) -> sky | (block<<8): the RG half of the
// RGBA8 texel in one u32 — a byte swap (little-endian byte order, matching
// the byte-wise path).
static inline uint32_t light_rg( const unsigned short packed ) {
    return static_cast<uint32_t>( ( packed >> 8 ) | ( ( packed & 0xFF ) << 8 ) );
}

// ---------------------------------------------------------------------------
// Upload-time light smoothing
//
// The chamfer flood already carries sub-level precision, so the residual
// stair-steps the 6-dir field used to show are gone — but a 3x3x3
// [1,2,1]-profile blur over the uploaded copy softens the remaining
// one-cell gradient kinks (Chunk::light[] stays exact). Neighbour gating
// mirrors the flood's connectivity so nothing blends where light can't
// flow:
//  - only cells that admit light contribute (solid stays out of the mix);
//  - a light floor (blocks_light_down) never mixes across its vertical
//    side — nothing descends into it and its sideways-held light isn't
//    smeared into the dark cell below;
//  - an edge diagonal contributes only through a shared face neighbour, so
//    a corner of solid blocks still separates the two sides;
//  - corner (3-axis) neighbours are skipped outright: tiny weight, and a
//    clean path check there needs three steps.
// The "Shadows" quality setting, runtime-toggleable from the pause menu.
// s_light_smooth_user holds the user's preference (-1 = not yet applied;
// init pushes the persisted choice). REPGAME_LIGHT_SMOOTH, when set, pins
// the mode for deterministic A/B runs.
static std::atomic<int> s_light_smooth_user{ -1 };
static int light_smooth_env_forced( ) {
    static const int forced = env_is_set( "REPGAME_LIGHT_SMOOTH" ) ? ( env_bool( "REPGAME_LIGHT_SMOOTH", true ) ? 1 : 0 ) : -1;
    return forced;
}
bool light_shadow_smooth_forced( ) {
    return light_smooth_env_forced( ) >= 0;
}
bool light_shadow_smooth( ) {
    const int forced = light_smooth_env_forced( );
    if ( forced >= 0 ) {
        return forced != 0;
    }
    const int u = s_light_smooth_user.load( std::memory_order_relaxed );
    return u < 0 ? true : u != 0;
}
void light_shadow_smooth_set( const bool on ) {
    s_light_smooth_user.store( on ? 1 : 0, std::memory_order_relaxed );
    pr_debug( "Light smoothing: %s%s", light_shadow_smooth( ) ? "on" : "off", light_shadow_smooth_forced( ) ? " (env-forced)" : "" );
}
bool light_smooth_enabled( ) {
    return light_shadow_smooth( );
}

// Smooth-mode chunks within this horizontal chunk radius of the camera get
// the blurred upload; outside it the raw field uploads as-is. The boundary
// delegation below keeps shared texels byte-exact across the ring, so the
// transition is invisible — and streamed-in far chunks upload cheap.
#define LIGHT_SMOOTH_RADIUS_DEFAULT 5
static int light_smooth_radius( ) {
    static const int r = env_int( "REPGAME_LIGHT_SMOOTH_RADIUS", LIGHT_SMOOTH_RADIUS_DEFAULT );
    return r;
}
bool light_smooth_in_radius( const glm::ivec3 &center, const glm::ivec3 &chunk_pos ) {
    const glm::ivec3 d = chunk_pos - center;
    const int r = light_smooth_radius( );
    return d.x * d.x + d.z * d.z <= r * r;
}
// Does this chunk's own upload smooth? Enabled + in radius; no loader
// (tests) keeps the old blanket behaviour.
static bool light_smooth_here( const ChunkLoader *loader, const glm::ivec3 &chunk_pos ) {
    if ( !light_smooth_enabled( ) ) {
        return false;
    }
    if ( loader == nullptr ) {
        return true;
    }
    return light_smooth_in_radius( loader->get_chunk_center( ), chunk_pos );
}
// Any of pos's 26 neighbours inside the smoothing radius? Pure position
// math — used to decide whether a raw chunk still needs the neighbour
// table (its halo texels delegate to smoothed owners).
static bool light_any_nb_in_radius( const glm::ivec3 &center, const glm::ivec3 &pos ) {
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dz = -1; dz <= 1; dz++ ) {
                if ( ( dx | dy | dz ) != 0 && light_smooth_in_radius( center, pos + glm::ivec3( dx, dy, dz ) ) ) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Predicate bundle so the smoother doesn't care whether opacity came from
// the cached bitmaps (mipmapped mode) or the cells[]/LUT walk.
struct LightSmoothCtx {
    const unsigned short *light;   // nullptr -> uniform volume (never smoothed)
    const BlockCell *cells;        // nullptr -> whole volume admits light
    const unsigned char *air_mask; // 1 bit/cell, cells[] index space
    const unsigned char *shade_mask;
    const unsigned char *air_lut;   // cells && !air_mask
    const unsigned char *shade_lut; // cells && !shade_mask
    // A texel on the volume edge (the halo ring) mirrors a neighbour chunk's
    // interior cell, and its 3x3x3 footprint reaches one cell PAST the data
    // this chunk holds — the blur needs a second ring that only the owner
    // has. nb[] carries the owning neighbours' bundles (indexed
    // (dx+1)*9+(dy+1)*3+(dz+1), nullptr when the neighbour isn't loaded) so a
    // boundary texel delegates to the exact value the owner uploads: both
    // chunks' textures then store the identical byte at the shared texel.
    // nb == nullptr -> one-sided gather (the border texel is biased slightly
    // low/high and the two chunks can disagree by a few LSB — visible seam).
    const LightSmoothCtx *const *nb;
    // Bit i (same (dx+1)*9+(dy+1)*3+(dz+1) indexing as nb) set when the
    // owner of that halo ring exists but uploads RAW (outside the smoothing
    // radius): our halo texel for it is the raw halo copy, which IS the
    // owner's canonical byte — no delegation needed.
    unsigned raw_owners;
    // Whether THIS chunk's own texels smooth (off for raw chunks that still
    // carry nb for delegation into smoothed neighbours).
    bool self_smooth;
    unsigned short uniform; // only consulted for a delegated owner with light == nullptr
};

// Callers pass the (private) chunk fields — Chunk/ChunkLoader member
// functions have access where this free helper does not.
static inline LightSmoothCtx light_smooth_ctx( const unsigned short *light, const BlockCell *cells,
                                               const unsigned char *air_mask, const unsigned char *shade_mask ) {
    LightSmoothCtx c;
    c.light = light;
    c.cells = cells;
    c.air_mask = air_mask;
    c.shade_mask = shade_mask;
    c.air_lut = cells && !air_mask ? light_air_lut( ) : nullptr;
    c.shade_lut = cells && !shade_mask ? light_shade_lut( ) : nullptr;
    c.nb = nullptr;
    c.raw_owners = 0;
    c.self_smooth = true;
    c.uniform = 0;
    return c;
}

// Fills nb_ctxs[]/nb_ptrs[] with the 26 neighbours' bundles around pos for
// the boundary delegation above. A Chunk method: it reads OTHER chunks'
// private light/cells/mask fields, which is legal inside the class but not
// from a free helper — ChunkLoader (a friend) calls it too.
// In-radius neighbours get a real bundle (halo texels delegate to their
// smoothed interior value); loaded out-of-radius neighbours set the
// raw_owners bit so our halo texel for them stays a raw copy — matching
// the raw byte they upload for the same position.
void Chunk::light_nb_ctxs( const ChunkLoader &loader, const glm::ivec3 &pos, const glm::ivec3 &center,
                           LightSmoothCtx nb_ctxs[ 27 ], const LightSmoothCtx *nb_ptrs[ 27 ], unsigned *raw_owners ) {
    *raw_owners = 0;
    for ( int dx = -1; dx <= 1; dx++ ) {
        for ( int dy = -1; dy <= 1; dy++ ) {
            for ( int dz = -1; dz <= 1; dz++ ) {
                const int i = ( dx + 1 ) * 9 + ( dy + 1 ) * 3 + ( dz + 1 );
                nb_ptrs[ i ] = nullptr;
                if ( dx == 0 && dy == 0 && dz == 0 ) {
                    continue;
                }
                Chunk *nb = loader.get_chunk( pos + glm::ivec3( dx, dy, dz ) );
                if ( nb == nullptr || nb->load_state != 0 || ( nb->light.volume == nullptr && nb->cells == nullptr ) ) {
                    continue; // absent/loading slot: boundary texels keep the one-sided gather
                }
                if ( !light_smooth_in_radius( center, pos + glm::ivec3( dx, dy, dz ) ) ) {
                    *raw_owners |= 1u << i; // owner uploads raw — our halo copy is its canonical byte
                    continue;
                }
                nb_ctxs[ i ] = light_smooth_ctx( nb->light.volume, nb->cells, nb->light.air_mask, nb->light.shade_mask );
                nb_ctxs[ i ].uniform = nb->light.uniform;
                nb_ptrs[ i ] = &nb_ctxs[ i ];
            }
        }
    }
}

// cells[]-order index (z-fastest) for the mask/BlockCell lookups.
static inline int light_cells_index( const int x, const int y, const int z ) {
    return ( y * CHUNK_SIZE_INTERNAL_X + x ) * CHUNK_SIZE_INTERNAL_Z + z;
}

static inline bool light_ctx_air( const LightSmoothCtx &c, const int x, const int y, const int z ) {
    if ( (unsigned)x >= CHUNK_SIZE_INTERNAL_X || (unsigned)y >= CHUNK_SIZE_INTERNAL_Y || (unsigned)z >= CHUNK_SIZE_INTERNAL_Z ) {
        return false;
    }
    if ( c.cells == nullptr ) {
        return true;
    }
    const int i = light_cells_index( x, y, z );
    if ( c.air_mask ) {
        return ( c.air_mask[ i >> 3 ] >> ( i & 7 ) ) & 1u;
    }
    return c.air_lut[ block_cell_id( c.cells[ i ] ) ] != 0;
}

// Is this cell a light floor (blocks_light_down)? Only called on in-bounds
// cells that already passed light_ctx_air.
static inline bool light_ctx_floor( const LightSmoothCtx &c, const int x, const int y, const int z ) {
    if ( c.cells == nullptr ) {
        return false;
    }
    const int i = light_cells_index( x, y, z );
    if ( c.shade_mask ) {
        return ( c.shade_mask[ i >> 3 ] >> ( i & 7 ) ) & 1u;
    }
    return c.shade_lut[ block_cell_id( c.cells[ i ] ) ] == 0;
}

// Can light cross the face adjacency a<->b? b must admit light, and a light
// floor seals the vertical adjacency in both directions.
static inline bool light_ctx_link( const LightSmoothCtx &c, const bool a_shade, const int ax, const int ay, const int az,
                                   const int bx, const int by, const int bz ) {
    if ( !light_ctx_air( c, bx, by, bz ) ) {
        return false;
    }
    if ( by != ay && ( a_shade || light_ctx_floor( c, bx, by, bz ) ) ) {
        return false;
    }
    return true;
}

// Smoothed (sky, block) for one texel, out[0]/out[1] in 0-255. Caller only
// invokes this on cells that admit light; c.light must be non-null.
static void light_ctx_smooth_cell( const LightSmoothCtx &c, const int x, const int y, const int z, unsigned char *out ) {
    const int lidx = ( z * CHUNK_SIZE_INTERNAL_Y + y ) * CHUNK_SIZE_INTERNAL_X + x;
    // Boundary texel (the halo ring mirrors a neighbour's interior cell):
    // compute the value the OWNER uploads so both chunks' textures hold the
    // identical byte. A local blur here would lack the neighbour ring past
    // the border and produce a few-LSB seam. Three owner cases, all
    // byte-exact: smoothed owner -> delegated value; raw (out-of-radius)
    // owner -> the raw halo copy, which is the byte it uploads; absent
    // owner -> our raw byte when we're raw, else the one-sided gather.
    const int ox = x == 0 ? -1 : x == CHUNK_SIZE_INTERNAL_X - 1 ? 1 : 0;
    const int oy = y == 0 ? -1 : y == CHUNK_SIZE_INTERNAL_Y - 1 ? 1 : 0;
    const int oz = z == 0 ? -1 : z == CHUNK_SIZE_INTERNAL_Z - 1 ? 1 : 0;
    if ( ( ox | oy | oz ) != 0 && c.nb != nullptr ) {
        const int slot = ( ox + 1 ) * 9 + ( oy + 1 ) * 3 + ( oz + 1 );
        const LightSmoothCtx *o = c.nb[ slot ];
        if ( o != nullptr ) {
            if ( o->light == nullptr ) {
                light_expand_cell( o->uniform, out );
            } else {
                // Our 0 -> owner's interior max (volume coord
                // CHUNK_SIZE_INTERNAL_*-2); our max -> its interior 0 (coord 1).
                const int nx = ox < 0 ? CHUNK_SIZE_INTERNAL_X - 2 : ox > 0 ? 1 : x;
                const int ny = oy < 0 ? CHUNK_SIZE_INTERNAL_Y - 2 : oy > 0 ? 1 : y;
                const int nz = oz < 0 ? CHUNK_SIZE_INTERNAL_Z - 2 : oz > 0 ? 1 : z;
                light_ctx_smooth_cell( *o, nx, ny, nz, out ); // interior: no re-delegation
            }
            return;
        }
        if ( ( c.raw_owners >> slot ) & 1u || !c.self_smooth ) {
            light_expand_cell( c.light[ lidx ], out );
            return;
        }
    }
    if ( !c.self_smooth ) {
        light_expand_cell( c.light[ lidx ], out );
        return;
    }
    const unsigned short packed = c.light[ lidx ];
    // Fast path: six identical face neighbours make the blur a no-op — this
    // covers every uniform region (full sky, deep shadow) for 6 reads.
    if ( x > 0 && x < CHUNK_SIZE_INTERNAL_X - 1 && y > 0 && y < CHUNK_SIZE_INTERNAL_Y - 1 && z > 0 && z < CHUNK_SIZE_INTERNAL_Z - 1 &&
         c.light[ lidx - 1 ] == packed && c.light[ lidx + 1 ] == packed &&
         c.light[ lidx - CHUNK_SIZE_INTERNAL_X ] == packed && c.light[ lidx + CHUNK_SIZE_INTERNAL_X ] == packed &&
         c.light[ lidx - CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y ] == packed &&
         c.light[ lidx + CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y ] == packed ) {
        light_expand_cell( packed, out );
        return;
    }
    static const int faces[ 6 ][ 3 ] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const bool c_shade = light_ctx_floor( c, x, y, z );
    // [1,2,1]^3 profile truncated to faces+edges: centre 8, face 4, edge 2.
    // Accumulate raw units — the byte channels already carry the sub-level
    // precision the blur is meant to preserve.
    int wsum = 8;
    int ssky = 8 * light_get_sky( packed ), sblk = 8 * light_get_block( packed );
    bool link[ 6 ];
    for ( int f = 0; f < 6; f++ ) {
        const int nx = x + faces[ f ][ 0 ], ny = y + faces[ f ][ 1 ], nz = z + faces[ f ][ 2 ];
        link[ f ] = light_ctx_link( c, c_shade, x, y, z, nx, ny, nz );
        if ( link[ f ] ) {
            const unsigned short np = c.light[ ( nz * CHUNK_SIZE_INTERNAL_Y + ny ) * CHUNK_SIZE_INTERNAL_X + nx ];
            wsum += 4;
            ssky += 4 * light_get_sky( np );
            sblk += 4 * light_get_block( np );
        }
    }
    for ( int i = 0; i < 6; i++ ) {
        for ( int j = i + 1; j < 6; j++ ) {
            const int mx = faces[ i ][ 0 ] + faces[ j ][ 0 ], my = faces[ i ][ 1 ] + faces[ j ][ 1 ], mz = faces[ i ][ 2 ] + faces[ j ][ 2 ];
            if ( mx == 0 && my == 0 && mz == 0 ) {
                continue; // opposite face pair — no diagonal between them
            }
            const int dx = x + mx, dy = y + my, dz = z + mz;
            if ( !light_ctx_air( c, dx, dy, dz ) || ( !link[ i ] && !link[ j ] ) ) {
                continue;
            }
            // The corner path C->M->D must be open through at least one of
            // the two shared face neighbours.
            const int m1x = x + faces[ i ][ 0 ], m1y = y + faces[ i ][ 1 ], m1z = z + faces[ i ][ 2 ];
            const int m2x = x + faces[ j ][ 0 ], m2y = y + faces[ j ][ 1 ], m2z = z + faces[ j ][ 2 ];
            const bool p1 = link[ i ] && light_ctx_link( c, light_ctx_floor( c, m1x, m1y, m1z ), m1x, m1y, m1z, dx, dy, dz );
            const bool p2 = link[ j ] && light_ctx_link( c, light_ctx_floor( c, m2x, m2y, m2z ), m2x, m2y, m2z, dx, dy, dz );
            if ( p1 || p2 ) {
                const unsigned short np = c.light[ ( dz * CHUNK_SIZE_INTERNAL_Y + dy ) * CHUNK_SIZE_INTERNAL_X + dx ];
                wsum += 2;
                ssky += 2 * light_get_sky( np );
                sblk += 2 * light_get_block( np );
            }
        }
    }
    int sky = ( ssky + wsum / 2 ) / wsum;
    int blk = ( sblk + wsum / 2 ) / wsum;
    // Maxed channels stay maxed: emitters and sun-shaft cells keep 255 so
    // the blur can't dim a torch face or an open-sky plateau.
    if ( light_get_sky( packed ) == LIGHT_RAW_MAX ) {
        sky = LIGHT_RAW_MAX;
    }
    if ( light_get_block( packed ) == LIGHT_RAW_MAX ) {
        blk = LIGHT_RAW_MAX;
    }
    out[ 0 ] = static_cast<unsigned char>( sky );
    out[ 1 ] = static_cast<unsigned char>( blk );
}

// Fill `dst` for [lo, lo+sz) in texel order (x fastest), with air cells' RG
// replaced by the smoothed value. mipmaps selects RGBA8 texels (B = air
// mask, A = shade) vs RG8 pairs. c.light must be non-null.
static void light_fill_scratch_smooth( const LightSmoothCtx &c, const glm::ivec3 lo, const glm::ivec3 sz, const bool mipmaps, unsigned char *dst ) {
    unsigned char *out8 = dst;
    for ( int z = 0; z < sz.z; z++ ) {
        const int gz = lo.z + z;
        for ( int y = 0; y < sz.y; y++ ) {
            const int gy = lo.y + y;
            for ( int x = 0; x < sz.x; x++, out8 += mipmaps ? 4 : 2 ) {
                const int gx = lo.x + x;
                const unsigned short packed = c.light[ ( gz * CHUNK_SIZE_INTERNAL_Y + gy ) * CHUNK_SIZE_INTERNAL_X + gx ];
                if ( !light_ctx_air( c, gx, gy, gz ) ) {
                    if ( mipmaps ) {
                        const uint32_t a = c.cells && light_ctx_floor( c, gx, gy, gz ) ? 0u : 255u;
                        *reinterpret_cast<uint32_t *>( out8 ) = light_rg( packed ) | ( a << 24 );
                    } else {
                        light_expand_cell( packed, out8 );
                    }
                    continue;
                }
                unsigned char s[ 2 ];
                light_ctx_smooth_cell( c, gx, gy, gz, s );
                if ( mipmaps ) {
                    const uint32_t a = c.cells && light_ctx_floor( c, gx, gy, gz ) ? 0u : 255u;
                    *reinterpret_cast<uint32_t *>( out8 ) = static_cast<uint32_t>( s[ 0 ] ) | ( static_cast<uint32_t>( s[ 1 ] ) << 8 ) | ( 255u << 16 ) | ( a << 24 );
                } else {
                    out8[ 0 ] = s[ 0 ];
                    out8[ 1 ] = s[ 1 ];
                }
            }
        }
    }
}

// Shared upload-box computation for the inline (render-thread) and staged
// (worker) fill paths. raw_box is the exchanged dirty texel box; returns the
// presentation mode to fill under (0 = nothing to do) with *uplo/*sz the
// texel box to fill: a smoothed chunk's box grows one cell (the blur reads a
// 1-cell footprint around each texel, so a change just inside the box
// rewrites texels one cell outside it), and a presentation-mode flip — the
// texture's last full fill ran under a different mode — upgrades to a full
// volume refill (a partial fill under the new mode would leave a mixed-mode
// texture). The flip also claims light_tex_flip so the detection only marks
// once, and re-marks the ring slabs this chunk owns in neighbours (their
// copies still carry the old mode's byte).
int ChunkLoader::light_upload_box( Chunk &chunk, const uint64_t raw_box, glm::ivec3 *uplo, glm::ivec3 *sz, bool *self_smooth_out ) {
    ChunkLoader *loader = this;
    const glm::ivec3 lo = glm::clamp( light_box_min( raw_box ), glm::ivec3( 0 ),
                                    glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) );
    const glm::ivec3 hi = glm::clamp( light_box_max( raw_box ), glm::ivec3( 0 ),
                                    glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) );
    if ( hi.x < lo.x || hi.y < lo.y || hi.z < lo.z ) {
        return 0;
    }
    const bool self_smooth = chunk.light.volume != nullptr && light_smooth_here( loader, chunk.chunk_pos );
    *self_smooth_out = self_smooth;
    const int want_mode = self_smooth ? 2 : 1;
    const int tex_mode = chunk.light.tex_mode.load( std::memory_order_relaxed );
    if ( tex_mode != 0 && tex_mode != want_mode ) {
        *uplo = glm::ivec3( 0 );
        *sz = glm::ivec3( CHUNK_SIZE_INTERNAL_X, CHUNK_SIZE_INTERNAL_Y, CHUNK_SIZE_INTERNAL_Z );
        if ( chunk.light.tex_flip.load( std::memory_order_relaxed ) != want_mode ) {
            chunk.light.tex_flip.store( want_mode, std::memory_order_relaxed );
            loader->light_remark_owned_halos( chunk );
        }
    } else if ( self_smooth ) {
        *uplo = glm::max( lo - glm::ivec3( 1 ), glm::ivec3( 0 ) );
        *sz = glm::min( hi + glm::ivec3( 1 ),
                        glm::ivec3( CHUNK_SIZE_INTERNAL_X - 1, CHUNK_SIZE_INTERNAL_Y - 1, CHUNK_SIZE_INTERNAL_Z - 1 ) ) -
              *uplo + glm::ivec3( 1 );
    } else {
        *uplo = lo;
        *sz = hi - lo + glm::ivec3( 1 );
    }
    return want_mode;
}

// Fill `dst` (tightly packed, UNPACK_ROW_LENGTH=0 layout) for the texel box
// [uplo, uplo+sz). Runs on the render thread (inline path) and on light
// workers (staging) — reads of chunk.light.volume/cells/masks against concurrent
// field writes are the same snapshot-torn reads the mark paths already
// tolerate; the result is a presentation copy, never fed back.
void ChunkLoader::light_fill_upload_box( Chunk &chunk, const glm::ivec3 &uplo, const glm::ivec3 &sz,
                                         const bool mipmaps, const bool self_smooth, unsigned char *dst ) {
    ChunkLoader &loader = *this;
    // Opacity lookups for B are skipped wherever the answer is already
    // known: null cells[] means the whole volume is air, and a lit
    // cell (packed != 0) provably can hold light — both give B = 255.
    // Only dark cells (solid vs. merely-dark air) need the mask — read
    // from chunk.light.air_mask (bit index = cells[] index space) when
    // it exists, else the cells[]/air_lut fallback.
    const bool all_air = mipmaps && chunk.cells == nullptr;
    const unsigned char *air_mask = mipmaps && !all_air ? chunk.light.air_mask : nullptr;
    const unsigned char *shade_mask = mipmaps && !all_air ? chunk.light.shade_mask : nullptr;
    const unsigned char *air_lut = mipmaps && chunk.cells && air_mask == nullptr ? light_air_lut( ) : nullptr;
    const unsigned char *shade_lut = mipmaps && chunk.cells && air_mask == nullptr ? light_shade_lut( ) : nullptr;
    // Boundary-texel delegation: the neighbour table is only needed when
    // the dirty box can actually reach the halo ring AND smoothing is
    // involved on some side of it — this chunk smoothed, or a raw chunk
    // whose halo texels delegate to an in-radius (smoothed) owner.
    const bool edge = uplo.x == 0 || uplo.y == 0 || uplo.z == 0 ||
        uplo.x + sz.x == CHUNK_SIZE_INTERNAL_X || uplo.y + sz.y == CHUNK_SIZE_INTERNAL_Y || uplo.z + sz.z == CHUNK_SIZE_INTERNAL_Z;
    const bool use_ctx = chunk.light.volume != nullptr &&
        ( self_smooth || ( edge && light_smooth_enabled( ) && light_any_nb_in_radius( loader.get_chunk_center( ), chunk.chunk_pos ) ) );
    if ( use_ctx ) {
        LightSmoothCtx nb_ctxs[ 27 ];
        const LightSmoothCtx *nb_ptrs[ 27 ] = {};
        unsigned raw_owners = 0;
        if ( edge ) {
            Chunk::light_nb_ctxs( loader, chunk.chunk_pos, loader.get_chunk_center( ), nb_ctxs, nb_ptrs, &raw_owners );
        }
        LightSmoothCtx ctx = light_smooth_ctx( chunk.light.volume, chunk.cells, chunk.light.air_mask, chunk.light.shade_mask );
        ctx.nb = nb_ptrs;
        ctx.raw_owners = raw_owners;
        ctx.self_smooth = self_smooth;
        light_fill_scratch_smooth( ctx, uplo, sz, mipmaps, dst );
    } else if ( mipmaps && air_mask ) {
        // Mask path: texel order (z, y, x — x fastest) so the light[]
        // rows and the dst both stream linearly; the mask bit index
        // steps +CHUNK_SIZE_INTERNAL_Z per x (see light_ensure_texture —
        // byte index (z>>3) + (Z_INT/8)*x + (X_INT*Z_INT/8)*y, tested bit z&7).
        uint32_t *out = reinterpret_cast<uint32_t *>( dst );
        for ( int z = 0; z < sz.z; z++ ) {
            const unsigned char *mrow = air_mask + ( ( uplo.z + z ) >> 3 );
            const unsigned char *srow = shade_mask ? shade_mask + ( ( uplo.z + z ) >> 3 ) : nullptr;
            const int shift = ( uplo.z + z ) & 7;
            for ( int y = 0; y < sz.y; y++ ) {
                const unsigned short *lrow = chunk.light.volume ? chunk.light.volume + ( ( uplo.z + z ) * CHUNK_SIZE_INTERNAL_Y + ( uplo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + uplo.x : nullptr;
                const unsigned char *mty = mrow + ( uplo.y + y ) * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 ) + uplo.x * ( CHUNK_SIZE_INTERNAL_Z / 8 );
                const unsigned char *sty = srow ? srow + ( uplo.y + y ) * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 ) + uplo.x * ( CHUNK_SIZE_INTERNAL_Z / 8 ) : nullptr;
                for ( int x = 0; x < sz.x; x++, out++ ) {
                    const unsigned short packed = lrow ? lrow[ x ] : chunk.light.uniform;
                    const uint32_t air = ( static_cast<uint32_t>( mty[ x * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u;
                    const uint32_t shade = sty ? ( ( static_cast<uint32_t>( sty[ x * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u ) : 0u;
                    *out = light_rg( packed ) | ( air * 255u << 16 ) | ( ( 255u - shade * 255u ) << 24 );
                }
            }
        }
    } else {
        for ( int y = 0; y < sz.y; y++ ) {
            if ( mipmaps ) {
                // Fallback (no mask): z-inner order as in
                // light_ensure_texture — cells[] reads (+1 index per z
                // step) stream sequentially while light[] and the
                // dst take the strided steps.
                for ( int x = 0; x < sz.x; x++ ) {
                    unsigned char *out = dst + 4 * ( y * sz.x + x );
                    const unsigned short *lrow = chunk.light.volume ? chunk.light.volume + ( ( uplo.z ) * CHUNK_SIZE_INTERNAL_Y + ( uplo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + uplo.x + x : nullptr;
                    int bidx = ( uplo.y + y ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( uplo.x + x ) * CHUNK_SIZE_INTERNAL_Z + uplo.z;
                    for ( int z = 0; z < sz.z; z++, out += 4 * sz.y * sz.x, bidx++ ) {
                        const unsigned short packed = lrow ? lrow[ z * CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_X ] : chunk.light.uniform;
                        light_expand_cell( packed, out );
                        out[ 2 ] = all_air || packed != 0 ? 255 : air_lut[ block_cell_id( chunk.cells[ bidx ] ) ];
                        out[ 3 ] = all_air ? 255 : shade_lut[ block_cell_id( chunk.cells[ bidx ] ) ];
                    }
                }
            } else {
                for ( int z = 0; z < sz.z; z++ ) {
                    unsigned char *out = dst + 2 * ( z * sz.y + y ) * sz.x;
                    if ( chunk.light.volume ) {
                        const unsigned short *row = chunk.light.volume + ( ( uplo.z + z ) * CHUNK_SIZE_INTERNAL_Y + ( uplo.y + y ) ) * CHUNK_SIZE_INTERNAL_X + uplo.x;
                        for ( int x = 0; x < sz.x; x++ ) {
                            light_expand_cell( row[ x ], out + 2 * x );
                        }
                    } else {
                        // Uniform volume: the whole dirty box reads light_uniform.
                        for ( int x = 0; x < sz.x; x++ ) {
                            light_expand_cell( chunk.light.uniform, out + 2 * x );
                        }
                    }
                }
            }
        }
    }
}

// Creating a light texture costs a full-volume fill + glTexImage3D
// (+ mipgen) on the render thread — ~0.5 ms with mipmaps on. Streaming
// bursts (finalize + first draws) can demand dozens in a single frame, so
// cap the creates per pass; over-budget chunks draw the shared fallback
// volume (light_fallback_texture) and retry next frame. Reset once per
// render pass by light_frame_begin.
#define LIGHT_ENSURE_MAX_PER_FRAME 2
static int s_light_ensure_left = LIGHT_ENSURE_MAX_PER_FRAME;

void light_frame_begin( ) {
    s_light_ensure_left = LIGHT_ENSURE_MAX_PER_FRAME;
}

// One shared 2^3 full-sky stand-in for chunks whose texture create ran out
// of the frame's budget (Chunk::draw binds it while light.texture is 0).
// It mirrors a real light texture's format and mip chain because the
// shader samples level 1 and the .b (air) / .a (shade) gates; the texel is
// sky-15 / block-0 / air / unshaded — the same content a fresh chunk's
// create fill produces — so the deferred chunk looks right until its real
// volume lands.
unsigned light_fallback_texture( ) {
    static GLuint tex = 0;
    if ( tex == 0 ) {
        const bool mipmaps = light_mipmaps_enabled( );
        glGenTextures( 1, &tex );
        glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
        glBindTexture( GL_TEXTURE_3D, tex );
        glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
        glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        unsigned char px[ 4 * 8 ] = {};
        if ( mipmaps ) {
            for ( int i = 0; i < 8; i++ ) {
                px[ 4 * i ] = 255;
                px[ 4 * i + 2 ] = 255;
                px[ 4 * i + 3 ] = 255;
            }
        } else {
            for ( int i = 0; i < 8; i++ ) {
                px[ 2 * i ] = 255;
            }
        }
        glTexImage3D( GL_TEXTURE_3D, 0, mipmaps ? GL_RGBA8 : GL_RG8, 2, 2, 2, 0,
                      mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, px );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        if ( mipmaps ) {
            glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, LIGHT_TEX_MAX_MIP );
            glGenerateMipmap( GL_TEXTURE_3D );
            glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
        } else {
            glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        }
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
    }
    return tex;
}

void Chunk::light_ensure_texture( ChunkLoader *loader ) {
    // Presentation mode for this texture: smooth only inside the camera
    // radius (and only when the feature is on).
    const bool self_smooth = this->light.volume != nullptr && light_smooth_here( loader, this->chunk_pos );
    const int want_mode = self_smooth ? 2 : 1;
    if ( this->light.texture != 0 ) {
        // The desired mode drifted since this texture was last filled
        // (Shadows toggle or a radius crossing): force a full-volume refill
        // and refresh the ring texels we own in neighbours.light.tex_flip
        // claims the drift once — the staged refill takes a few frames, and
        // re-marking the neighbours every drawn frame until it lands would
        // flood their upload queues.
        if ( loader != nullptr && this->light.volume != nullptr && this->light.tex_mode != 0 &&
             this->light.tex_mode != want_mode && this->light.tex_flip != want_mode ) {
            this->light.tex_flip = want_mode;
            loader->light_mark_dirty_all( *this );
            loader->light_remark_owned_halos( *this );
        }
        return;
    }
    // Create-budget miss: skip the fill/upload — Chunk::draw binds the
    // shared fallback volume while texture stays 0, and the next drawn
    // frame retries the create. Don't touch tex_flip or the dirty marks;
    // nothing was created for them to apply to.
    if ( s_light_ensure_left <= 0 ) {
        return;
    }
    s_light_ensure_left--;
    // A raw chunk with an in-radius neighbour still owes one staged fill so
    // its halo texels take the owner's smoothed byte — see
    // light_fill_upload_box's delegation.
    const bool want_nb = loader != nullptr && light_smooth_enabled( ) && this->light.volume != nullptr &&
        light_any_nb_in_radius( loader->get_chunk_center( ), this->chunk_pos );
    const long long t0 = light_now_us( );
    glGenTextures( 1, &this->light.texture );
    glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
    glBindTexture( GL_TEXTURE_3D, this->light.texture );
    // Full upload at creation: a chunk can be drawn before its first
    // light_upload_dirty pass, and must never sample an uninitialized volume.
    // Covers every queued dirty region, so clear the flag.
    // The block atlas (Texture::loadTexture) leaves UNPACK_ROW_LENGTH /
    // UNPACK_IMAGE_HEIGHT set, and the default UNPACK_ALIGNMENT=4 pads our
    // rows — set all three explicitly or the upload reads garbage.
    // Consume the dirty box BEFORE copying so marks landing mid-upload stay
    // pending (the chunk may not be in the list, but a mark sees listed==0
    // or the queued entry itself picks them up next pass).
    this->light.dirty_box.exchange( LIGHT_BOX_EMPTY, std::memory_order_acq_rel );
    const bool mipmaps = light_mipmaps_enabled( );
    if ( mipmaps ) {
        unsigned char *out8 = reinterpret_cast<unsigned char *>( s_light_upload_scratch );
        // B is a per-cell air mask. It normally comes straight out of
        // chunk.light.air_mask — the 32KB bitmap light_fill_chunk built while
        // streaming cells[] and edits keep stamped — so the common case
        // iterates in texel order and never touches the 3.7MB cells[] array
        // (nor even strides light[]: both stream linearly). When the mask is
        // missing the fallback still walks cells[] (BlockState is 14 packed
        // bytes) in z-inner order so the cells[] side streams. Two fast
        // paths skip the general case entirely:
        //  - blocks == null: the whole volume is air, B = 255.
        //  - light == null && light_uniform != 0: every cell holds light, so
        //    every cell is non-opaque by definition — B = 255, no lookup.
        //    (Uniform light alone is NOT enough: an all-stone chunk packs
        //    light_uniform == 0 just like an unlit air pocket.)
        //    The uniform-bright case additionally requires no per-cell A
        //    lookups: light floors (blocks_light_down) get A=0, so the
        //    constant fill is only safe when cells[] is absent or the
        //    shade mask isn't there to contradict it — the mask/fallback
        //    paths below stamp A per cell.
        if ( this->light.volume == nullptr && ( this->cells == nullptr || ( this->light.uniform != 0 && this->light.air_mask == nullptr ) ) ) {
            const uint32_t cell = light_rg( this->light.uniform ) | 0xFFFF0000u; // B=255, A=255
            uint32_t *out32 = s_light_upload_scratch;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                out32[ i ] = cell;
            }
        } else if ( this->cells == nullptr ) {
            // All-air but non-uniform light: still expand light[] per cell.
            uint32_t *out32 = s_light_upload_scratch;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                out32[ i ] = light_rg( this->light.volume[ i ] ) | 0xFFFF0000u;
            }
        } else if ( this->light.air_mask != nullptr ) {
            // Mask built: iterate in texel order (x fastest) so light[] reads
            // and scratch writes are both linear streams. The mask lives in
            // cells[] index space (z-fastest): mask byte index is
            // (tz>>3) + (Z_INT/8)*tx + (X_INT*Z_INT/8)*ty and the tested bit
            // (tz&7) is invariant across the plane, so the same
            // X_INT*Z_INT/8 mask bytes serve 8 consecutive tz planes.
            const unsigned char *air_mask = this->light.air_mask;
            const unsigned char *shade_mask = this->light.shade_mask;
            uint32_t *out32 = s_light_upload_scratch;
            if ( this->light.volume == nullptr ) {
                // Uniform-dark volume (light_uniform==0 — nonzero went to the
                // fast path above): every cell's RG is the same constant and
                // B is exactly the mask bit, so no per-cell light read.
                const uint32_t base = light_rg( this->light.uniform );
                for ( int tz = 0; tz < CHUNK_SIZE_INTERNAL_Z; tz++ ) {
                    const unsigned char *mrow = air_mask + ( tz >> 3 );
                    const unsigned char *srow = shade_mask ? shade_mask + ( tz >> 3 ) : nullptr;
                    const int shift = tz & 7;
                    for ( int ty = 0; ty < CHUNK_SIZE_INTERNAL_Y; ty++ ) {
                        const unsigned char *mty = mrow + ty * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 );
                        const unsigned char *sty = srow ? srow + ty * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 ) : nullptr;
                        for ( int tx = 0; tx < CHUNK_SIZE_INTERNAL_X; tx++, out32++ ) {
                            const uint32_t air = ( static_cast<uint32_t>( mty[ tx * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u;
                            // A=0 marks light floors for the contact-shadow
                            // tap; 255 for everything else.
                            const uint32_t shade = sty ? ( ( static_cast<uint32_t>( sty[ tx * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u ) : 0u;
                            *out32 = base | ( air * 255u << 16 ) | ( ( 255u - shade * 255u ) << 24 );
                        }
                    }
                }
            } else {
                for ( int tz = 0; tz < CHUNK_SIZE_INTERNAL_Z; tz++ ) {
                    const unsigned char *mrow = air_mask + ( tz >> 3 );
                    const unsigned char *srow = shade_mask ? shade_mask + ( tz >> 3 ) : nullptr;
                    const int shift = tz & 7;
                    for ( int ty = 0; ty < CHUNK_SIZE_INTERNAL_Y; ty++ ) {
                        const unsigned short *lrow = this->light.volume + ( tz * CHUNK_SIZE_INTERNAL_Y + ty ) * CHUNK_SIZE_INTERNAL_X;
                        const unsigned char *mty = mrow + ty * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 );
                        const unsigned char *sty = srow ? srow + ty * ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z / 8 ) : nullptr;
                        for ( int tx = 0; tx < CHUNK_SIZE_INTERNAL_X; tx++, out32++ ) {
                            const unsigned short packed = lrow[ tx ];
                            // The mask is authoritative for opacity — a lit
                            // cell is provably non-opaque (its bit is set),
                            // and a solid cell reads B=0 even while its light
                            // is still propagating out.
                            const uint32_t air = ( static_cast<uint32_t>( mty[ tx * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u;
                            const uint32_t shade = sty ? ( ( static_cast<uint32_t>( sty[ tx * ( CHUNK_SIZE_INTERNAL_Z / 8 ) ] ) >> shift ) & 1u ) : 0u;
                            *out32 = light_rg( packed ) | ( air * 255u << 16 ) | ( ( 255u - shade * 255u ) << 24 );
                        }
                    }
                }
            }
        } else {
            // Fallback for a chunk whose mask was never built: walk cells[]
            // alongside light[]. The two arrays transpose (light[] x-fastest,
            // cells[] z-fastest), so iterate z innermost — cells[] reads
            // stream sequentially while the strided accesses move to the
            // small cache-resident arrays (light[] is 512KB, scratch 1MB).
            const unsigned char *air_lut = light_air_lut( );
            const unsigned char *shade_lut = light_shade_lut( );
            int tx = 0, ty = 0, tz = 0;
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                const int lidx = ( tz * CHUNK_SIZE_INTERNAL_Y + ty ) * CHUNK_SIZE_INTERNAL_X + tx;
                const unsigned short packed = this->light.volume ? this->light.volume[ lidx ] : this->light.uniform;
                unsigned char *out = out8 + 4 * lidx;
                light_expand_cell( packed, out );
                out[ 2 ] = packed != 0 ? 255 : air_lut[ block_cell_id( this->cells[ i ] ) ];
                out[ 3 ] = shade_lut[ block_cell_id( this->cells[ i ] ) ];
                if ( ++tz == CHUNK_SIZE_INTERNAL_Z ) {
                    tz = 0;
                    if ( ++tx == CHUNK_SIZE_INTERNAL_X ) {
                        tx = 0;
                        ty++;
                    }
                }
            }
        }
    } else {
        unsigned char *out8 = reinterpret_cast<unsigned char *>( s_light_upload_scratch );
        if ( this->light.volume ) {
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                light_expand_cell( this->light.volume[ i ], out8 + 2 * i );
            }
        } else {
            // Uniform volume (null light[]): the whole texture is one value.
            unsigned char cell[ 2 ];
            light_expand_cell( this->light.uniform, cell );
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                out8[ 2 * i ] = cell[ 0 ];
                out8[ 2 * i + 1 ] = cell[ 1 ];
            }
        }
    }
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexImage3D( GL_TEXTURE_3D, 0, mipmaps ? GL_RGBA8 : GL_RG8, CHUNK_SIZE_INTERNAL_X, CHUNK_SIZE_INTERNAL_Y, CHUNK_SIZE_INTERNAL_Z, 0, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    if ( mipmaps ) {
        // LIGHT_TEX_MAX_MIP mirrors the shader's LIGHT_LOD_MAX cap: levels
        // deeper than 1 are never sampled, so cap the chain and let
        // glGenerateMipmap build only level 1 (32^3) instead of all six.
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, LIGHT_TEX_MAX_MIP );
        glGenerateMipmap( GL_TEXTURE_3D );
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
    } else {
        glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    }
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
    // The creation fill is always raw — the smoothed/delegated fill runs on
    // a light worker via the staging queue so ensure stays off the ~3ms
    // smoother during streaming. Raw bytes differ from the smoothed result
    // by a few LSB at most, invisible for the frames it takes to land.
    this->light.tex_mode = 1;
    // Whole volume now holds this position's field — the texture can be
    // bound again (see Chunk::draw's tex_stale fallback).
    this->light.tex_stale.store( 0, std::memory_order_relaxed );
    if ( loader != nullptr && self_smooth ) {
        // Claim the flip so repeated ensures don't re-mark while the staged
        // full-volume fill is in flight.
        this->light.tex_flip = 2;
        loader->light_mark_dirty_all( *this );
        loader->light_remark_owned_halos( *this );
    } else if ( want_nb ) {
        loader->light_mark_dirty_all( *this );
    }
    light_dbg_n_ensure++;
    light_dbg_us_ensure += light_now_us( ) - t0;
}

void ChunkLoader::light_upload_dirty( const int max_uploads, const long long budget_us ) {
    // The shared frame-work budget can leave this pass nothing — clamp to
    // the floor so uploads keep trickling anyway, and cap it so a deep
    // backlog can't reclaim the whole frame.
    const long long budget = std::min<long long>( LIGHT_UPLOAD_BUDGET_US,
                                                  std::max<long long>( LIGHT_UPLOAD_FLOOR_US, budget_us ) );
    const long long t0 = light_now_us( );
    if ( light_dirty_list_near.empty( ) && light_dirty_list_far.empty( ) ) {
        light_dbg_us_upload += light_now_us( ) - t0;
        return;
    }
    struct LightUploadEntry {
        Chunk *chunk;
        uint64_t box;
        bool staged; // light_stage_buf holds the fill — no box exchange
    };
    static std::vector<LightUploadEntry> entries;
    entries.clear( );
    const long tick_now = light_tick_counter.load( std::memory_order_relaxed );
    static std::vector<Chunk *> eligible;
    bool wake_stage = false;
    {
        // Clear listed AND snapshot the dirty box in the same critical
        // section that detaches the chunk from the list: a mark whose CAS
        // misses this exchange necessarily enters the mutex after us, sees
        // listed==0, and re-adds the chunk — the next frame's exchange picks
        // its texel up. Entries beyond the cap stay in the list untouched.
        // Eligibility = write-quiet debounce + in-progress gate. Quiet:
        // chunks written within LIGHT_UPLOAD_QUIET_TICKS stay listed so the
        // GPU never snapshots a mid-march volume. In-progress: a column with
        // pending seeds or a held lock is still mid-flood — its floor-write
        // to respread-write gap under a deep backlog is far longer than any
        // tick window, so counting the work itself is what suppresses the
        // correct→dark→correct flash. Defers stay capped so a column under
        // a sustained march can't starve its upload forever.
        std::lock_guard<std::mutex> lock( light_work_mutex );
        size_t n = max_uploads;
        // Near uploads first — the visible region must not wait behind the
        // far backlog when the per-frame cap is binding.
        for ( std::vector<Chunk *> *list : { &light_dirty_list_near, &light_dirty_list_far } ) {
            eligible.clear( );
            for ( Chunk *chunk : *list ) {
                // Fresh chunks bypass both gates: the optimistic sky fill
                // is mostly correct and beats a black hole while seeds are
                // still queued — later uploads still wait for the flood.
                const bool fresh = !chunk->light.uploaded_once;
                const int cidx = light_col_lock_index( chunk->chunk_pos.x, chunk->chunk_pos.z );
                const bool busy = light_col_incident[ cidx ].load( std::memory_order_relaxed ) > 0 ||
                                  light_col_locks[ cidx ].load( std::memory_order_relaxed ) != 0;
                const bool quiet = tick_now - chunk->light.write_tick.load( std::memory_order_relaxed ) >= LIGHT_UPLOAD_QUIET_TICKS;
                const int cap = busy ? LIGHT_UPLOAD_MAX_DEFERS_BUSY : LIGHT_UPLOAD_MAX_DEFERS;
                const unsigned stage_status = LIGHT_STAGE_STATUS( chunk->light.stage_state.load( std::memory_order_relaxed ) );
                // BUSY stays listed untouched — the publishing worker
                // re-enqueues it, and it's not waiting on eligibility so it
                // doesn't accrue defers. READY is always eligible: the
                // quiet/busy/defer gates were already applied when the chunk
                // was claimed for staging, so gating the finished buffer
                // again just parks pool buffers behind a debounce window.
                // IDLE takes the same gates as before: a snapshot of a
                // mid-march volume is a mid-march upload — the quiet/defer
                // rules exist to keep exactly those off the GPU.
                if ( stage_status == LIGHT_STAGE_READY ||
                     ( stage_status != LIGHT_STAGE_BUSY &&
                       ( fresh || ( quiet && !busy ) || chunk->light.upload_defers >= cap ) ) ) {
                    eligible.push_back( chunk );
                } else if ( stage_status != LIGHT_STAGE_BUSY ) {
                    chunk->light.upload_defers++;
                    light_dbg_upload_deferred++;
                }
            }
            // Nearest of the eligible backlog first so the upload budget
            // lands on the visible region. Keys are snapshotted before the
            // sort: chunk_pos is rewritten by the terrain thread when a slot
            // recycles, and a comparator whose answers change mid-sort lets
            // std::sort's unguarded insertion run off the array (segfault).
            static std::vector<std::pair<int, Chunk *>> order;
            order.clear( );
            for ( Chunk *chunk : eligible ) {
                const glm::ivec3 d = chunk->chunk_pos - this->chunk_center;
                order.emplace_back( d.x * d.x + d.z * d.z, chunk );
            }
            std::sort( order.begin( ), order.end( ),
                       []( const std::pair<int, Chunk *> &a, const std::pair<int, Chunk *> &b ) { return a.first < b.first; } );
            entries.reserve( entries.size( ) + order.size( ) );
            for ( const std::pair<int, Chunk *> &ord : order ) {
                Chunk *chunk = ord.second;
                unsigned s = chunk->light.stage_state.load( std::memory_order_relaxed );
                const bool ready = LIGHT_STAGE_STATUS( s ) == LIGHT_STAGE_READY;
                // A smoothed fill costs ~25x the raw expand — hand it to a
                // light worker (the claim CAS keeps the handoff
                // single-shot; the worker re-enqueues on publish). That
                // covers the just-outside-radius shell too: those chunks
                // still run the ctx kernel for owner-delegated halo texels.
                // Genuinely raw chunks keep the inline fill: their expand
                // is already cheap, so a round trip would only add latency.
                // No async lighting (WASM without pthreads, tests) means
                // nobody would service the stage queue — stay inline there.
                const bool wants_stage = !ready && chunk->light.volume != nullptr && light_async_active( ) && light_smooth_enabled( ) &&
                    ( light_smooth_in_radius( this->chunk_center, chunk->chunk_pos ) ||
                      light_any_nb_in_radius( this->chunk_center, chunk->chunk_pos ) );
                // READY uploads return a pool buffer, so they skip the
                // count budget — parking them behind nearer IDLE work
                // starved the pool. They still wait on the pass's
                // wall-clock budget below: leftovers re-queue and drain
                // over the next frames. A stage claim
                // only goes out when a buffer is actually free (claims
                // queued but not yet started count as taken): a claim that
                // can't get one would bounce straight back and burn the
                // budget every frame, starving everything behind it —
                // including far raw chunks that need no buffer at all.
                if ( !ready && n == 0 ) {
                    continue;
                }
                if ( wants_stage && light_stage_live + static_cast<int>( light_stage_list.size( ) ) >= LIGHT_STAGE_MAX ) {
                    continue; // stays listed for a later pass
                }
                chunk->light.upload_listed.store( 0, std::memory_order_release );
                chunk->light.upload_defers = 0;
                chunk->light.uploaded_once = 1;
                // Taking a READY buffer consumes only what the worker
                // snapshotted; marks that landed during the stage are still
                // in the box, and their enqueue found the chunk already
                // listed. Keep it listed (it survives the rebuild below) so
                // the next pass uploads them. The no-op RMW orders this read
                // against concurrent mark CASes the way the IDLE path's
                // exchange does: a mark after it sees listed==0 and
                // re-enqueues itself.
                const auto take_ready = [ & ]( ) {
                    if ( chunk->light.dirty_box.fetch_add( 0, std::memory_order_acq_rel ) != LIGHT_BOX_EMPTY ) {
                        chunk->light.upload_listed.store( 1, std::memory_order_relaxed );
                    }
                    entries.push_back( { chunk, 0, true } );
                };
                if ( ready ) {
                    // The worker already exchanged the box and filled the
                    // staged buffer — just upload it.
                    take_ready( );
                    continue;
                }
                n--;
                if ( wants_stage && chunk->light.stage_state.compare_exchange_strong( s, s + 1, std::memory_order_acq_rel, std::memory_order_relaxed ) ) {
                    light_stage_list.push_back( chunk );
                    wake_stage = true;
                    continue;
                }
                // The CAS can only fail if a worker published mid-pass —
                // its staged buffer is newer than what we'd fill inline.
                if ( LIGHT_STAGE_STATUS( s ) == LIGHT_STAGE_READY ) {
                    take_ready( );
                    continue;
                }
                entries.push_back( { chunk, chunk->light.dirty_box.exchange( LIGHT_BOX_EMPTY, std::memory_order_acq_rel ), false } );
            }
            // Rebuild minus taken entries (they're the ones now listed==0).
            std::vector<Chunk *> keep;
            keep.reserve( list->size( ) );
            for ( Chunk *chunk : *list ) {
                if ( chunk->light.upload_listed.load( std::memory_order_relaxed ) ) {
                    keep.push_back( chunk );
                }
            }
            list->swap( keep );
        }
    }
    if ( wake_stage ) {
        light_work_cv.notify_all( );
    }
    int uploads = 0;
    const bool mipmaps = light_mipmaps_enabled( );
    size_t eidx = 0;
    for ( ; eidx < entries.size( ); eidx++ ) {
        const LightUploadEntry &e = entries[ eidx ];
        // Time-box the pass (budget): always land the first upload so a
        // pathological frame can't starve the queue entirely.
        if ( uploads > 0 && light_now_us( ) - t0 > budget ) {
            break;
        }
        Chunk &chunk = *e.chunk;
        if ( e.staged ) {
            // Worker-produced fill: validate and upload. load_state means
            // the slot is being recycled — release_terrain's
            // light_reset_staging reclaims the buffer, so just skip.
            if ( chunk.load_state || chunk.light.texture == 0 ) {
                continue;
            }
            unsigned s = chunk.light.stage_state.load( std::memory_order_acquire );
            if ( LIGHT_STAGE_STATUS( s ) != LIGHT_STAGE_READY ) {
                continue;
            }
            // Take ownership with the CAS, not a post-upload store: a
            // light_reset_staging on the terrain thread can flip the state
            // and hand the same buffer out again — a failed CAS means reset
            // owns it and we must not touch the fields. READY -> IDLE, same
            // generation. Marks that landed during the stage already
            // re-enqueued the chunk for the next pass.
            unsigned char *buf = chunk.light.stage_buf;
            const uint64_t stage_box = chunk.light.stage_box;
            const int stage_mode = chunk.light.stage_mode;
            if ( buf == nullptr ||
                 !chunk.light.stage_state.compare_exchange_strong( s, s - 2,
                                                                 std::memory_order_acq_rel, std::memory_order_relaxed ) ) {
                continue;
            }
            chunk.light.stage_buf = nullptr;
            const glm::ivec3 ulo = light_box_min( stage_box );
            const glm::ivec3 usz = light_box_max( stage_box ) - ulo + glm::ivec3( 1 );
            uploads++;
            glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
            glBindTexture( GL_TEXTURE_3D, chunk.light.texture );
            glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
            glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
            glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
            glTexSubImage3D( GL_TEXTURE_3D, 0, ulo.x, ulo.y, ulo.z, usz.x, usz.y, usz.z, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, buf );
            glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
            if ( mipmaps ) {
                glGenerateMipmap( GL_TEXTURE_3D );
            }
            // A full-volume fill settles the texture's presentation mode;
            // partial fills keep whatever the last full fill used.
            if ( usz.x == CHUNK_SIZE_INTERNAL_X && usz.y == CHUNK_SIZE_INTERNAL_Y && usz.z == CHUNK_SIZE_INTERNAL_Z ) {
                chunk.light.tex_mode.store( stage_mode, std::memory_order_relaxed );
                if ( chunk.light.tex_flip == stage_mode ) {
                    chunk.light.tex_flip = 0;
                }
                chunk.light.tex_stale.store( 0, std::memory_order_relaxed );
            }
            light_stage_pool_give( buf );
            continue;
        }
        if ( e.box == LIGHT_BOX_EMPTY || chunk.load_state ) {
            continue;
        }
        // No texture yet: light_ensure_texture's full upload at first draw
        // already covers every dirty texel — skip the allocation here so
        // never-drawn (faceless) chunks don't pay ~1.1MB of VRAM each.
        // Face-bearing chunks are ensured at finalize (ChunkLoader::
        // finalize_loaded_chunk), so a drawable chunk reaching this point
        // without a texture is rare — an edit making a faceless chunk
        // drawable, or an upload queued before its first-ever draw.
        if ( chunk.light.texture == 0 ) {
            continue;
        }
        glm::ivec3 uplo, sz;
        bool self_smooth;
        const int want_mode = this->light_upload_box( chunk, e.box, &uplo, &sz, &self_smooth );
        if ( want_mode == 0 ) {
            continue;
        }
        uploads++;
        glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
        glBindTexture( GL_TEXTURE_3D, chunk.light.texture );
        // Expand the packed (sky<<8|block) dirty region so the two channels
        // filter independently — see light_expand_cell / light_air_lut. The
        // scratch is tightly packed so the default unpack stride applies.
        this->light_fill_upload_box( chunk, uplo, sz, mipmaps, self_smooth,
                                     reinterpret_cast<unsigned char *>( s_light_upload_scratch ) );
        glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
        glPixelStorei( GL_UNPACK_IMAGE_HEIGHT, 0 );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        glTexSubImage3D( GL_TEXTURE_3D, 0, uplo.x, uplo.y, uplo.z, sz.x, sz.y, sz.z, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, s_light_upload_scratch );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        if ( mipmaps ) {
            glGenerateMipmap( GL_TEXTURE_3D );
        }
        // A full-volume fill settles the texture's presentation mode;
        // partial fills keep whatever mode the last full fill used.
        if ( sz.x == CHUNK_SIZE_INTERNAL_X && sz.y == CHUNK_SIZE_INTERNAL_Y && sz.z == CHUNK_SIZE_INTERNAL_Z ) {
            chunk.light.tex_mode = want_mode;
            if ( chunk.light.tex_flip == want_mode ) {
                chunk.light.tex_flip = 0;
            }
            chunk.light.tex_stale.store( 0, std::memory_order_relaxed );
        }
    }
    // Re-queue whatever the budget left behind: staged buffers stay READY on
    // their chunk (next pass picks them up again), inline entries already
    // exchanged their box out so merge it back or those texels are lost. A
    // load_state slot was recycled mid-pass — light_reset_staging already
    // reclaimed its staged buffer — so don't re-list it.
    for ( ; eidx < entries.size( ); eidx++ ) {
        LightUploadEntry &e = entries[ eidx ];
        Chunk &chunk = *e.chunk;
        if ( chunk.load_state ) {
            continue;
        }
        if ( !e.staged ) {
            if ( e.box == LIGHT_BOX_EMPTY ) {
                continue; // nothing was exchanged out — nothing to restore
            }
            uint64_t box = chunk.light.dirty_box.load( std::memory_order_relaxed );
            for ( ;; ) {
                const uint64_t merged = light_box_union( box, e.box );
                if ( chunk.light.dirty_box.compare_exchange_weak( box, merged,
                                                                  std::memory_order_acq_rel, std::memory_order_relaxed ) ) {
                    break;
                }
            }
        }
        this->light_upload_enqueue( chunk );
    }
    light_dbg_n_upload += uploads;
    light_dbg_us_upload += light_now_us( ) - t0;
}

// ---------------------------------------------------------------------------
// Worker-side upload staging
// ---------------------------------------------------------------------------

// Fill a pooled buffer with the chunk's dirty texels — the smoothed
// presentation fill that light_upload_dirty used to run synchronously on
// the render thread. Called on a light worker; needs no column claim: it
// only reads the field (the same snapshot-torn reads the mark paths make)
// and writes its private buffer. Publishes as READY so the next
// light_upload_dirty pass does just the GL call.
void ChunkLoader::light_stage_chunk( Chunk &chunk ) {
    const unsigned claimed = chunk.light.stage_state.load( std::memory_order_relaxed );
    if ( LIGHT_STAGE_STATUS( claimed ) != LIGHT_STAGE_BUSY ) {
        return; // stale queue entry — nobody claimed this chunk
    }
    auto unclaim = [ &chunk, claimed ]( ) {
        // CAS not store: a recycle bumps the generation — never write a
        // stale word back.
        unsigned expected = claimed;
        chunk.light.stage_state.compare_exchange_strong( expected, claimed - 1,
                                                         std::memory_order_release, std::memory_order_relaxed );
    };
    // Slot being recycled: back out so the slot doesn't stall BUSY — the
    // reset's generation bump either already orphaned us (CAS fails) or
    // runs after our unclaim and bumps IDLE to the next generation.
    if ( chunk.load_state ) {
        unclaim( );
        return;
    }
    // Take the staging buffer before touching the dirty box: an empty pool
    // must leave the box intact for the re-enqueued retry.
    unsigned char *buf = light_stage_pool_take( );
    if ( buf == nullptr ) {
        unclaim( );
        this->light_upload_enqueue( chunk ); // retry once a buffer frees up
        return;
    }
    // Exchange the dirty box here (not at render pop): marks landing before
    // this exchange fold into the fill; marks after it re-enqueue for the
    // next stage — the same no-lost-update ordering the old pop-time
    // exchange guaranteed.
    const uint64_t box = chunk.light.dirty_box.exchange( LIGHT_BOX_EMPTY, std::memory_order_acq_rel );
    if ( box == LIGHT_BOX_EMPTY || chunk.light.texture == 0 ) {
        light_stage_pool_give( buf );
        unclaim( );
        return;
    }
    glm::ivec3 uplo, sz;
    bool self_smooth;
    const int mode = this->light_upload_box( chunk, box, &uplo, &sz, &self_smooth );
    if ( mode == 0 ) {
        light_stage_pool_give( buf );
        unclaim( );
        return;
    }
    const long long t0 = light_now_us( );
    this->light_fill_upload_box( chunk, uplo, sz, light_mipmaps_enabled( ), self_smooth, buf );
    light_dbg_us_fill += light_now_us( ) - t0;
    light_dbg_n_fill++;
    // Publish: plain field stores, then the release CAS makes them visible
    // alongside READY. The generation in `claimed` can only have changed via
    // light_reset_staging (recycle) — fail then and free the buffer
    // ourselves.
    chunk.light.stage_buf = buf;
    chunk.light.stage_box = light_box_pack( uplo, uplo + sz - glm::ivec3( 1 ) );
    chunk.light.stage_mode = mode;
    unsigned expected = claimed;
    if ( chunk.light.stage_state.compare_exchange_strong( expected, claimed + 1,
                                                          std::memory_order_release, std::memory_order_relaxed ) ) {
        this->light_upload_enqueue( chunk );
    } else {
        chunk.light.stage_buf = nullptr;
        light_stage_pool_give( buf );
    }
}

void ChunkLoader::light_debug_verify( ) {
    long long cells = 0, under[ 2 ] = { 0, 0 }, over[ 2 ] = { 0, 0 }, mirror_bad = 0;
    int logged = 0;
    const auto settled = []( const Chunk *c ) { return c && !c->load_state && c->light.finalized.load( std::memory_order_relaxed ); };
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !settled( &chunk ) ) {
            continue;
        }
        const Chunk *nb[ 27 ];
        for ( int k = 0; k < 27; k++ ) {
            const glm::ivec3 o( k / 9 - 1, ( k / 3 ) % 3 - 1, k % 3 - 1 );
            nb[ k ] = o == glm::ivec3( 0 ) ? &chunk : this->get_chunk( chunk.chunk_pos + o );
        }
        const auto owner_of = [ & ]( const glm::ivec3 &l, glm::ivec3 &l_out ) -> const Chunk * {
            const glm::ivec3 o( l.x < 0 ? -1 : l.x >= CHUNK_SIZE_X ? 1 : 0, l.y < 0 ? -1 : l.y >= CHUNK_SIZE_Y ? 1 : 0, l.z < 0 ? -1 : l.z >= CHUNK_SIZE_Z ? 1 : 0 );
            l_out = l - o * CHUNK_SIZE_I;
            const Chunk *c = nb[ ( o.x + 1 ) * 9 + ( o.y + 1 ) * 3 + ( o.z + 1 ) ];
            return settled( c ) ? c : nullptr;
        };
        for ( int x = -1; x <= CHUNK_SIZE_X; x++ ) {
            for ( int y = -1; y <= CHUNK_SIZE_Y; y++ ) {
                for ( int z = -1; z <= CHUNK_SIZE_Z; z++ ) {
                    const glm::ivec3 c( x, y, z );
                    const bool halo = x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE_X || y >= CHUNK_SIZE_Y || z >= CHUNK_SIZE_Z;
                    const unsigned short cell = light_read_cell( chunk, light_index( x, y, z ) );
                    if ( halo ) {
                        glm::ivec3 ol;
                        const Chunk *owner = owner_of( c, ol );
                        if ( owner && light_read_cell( *owner, light_index( ol.x, ol.y, ol.z ) ) != cell ) {
                            mirror_bad++;
                        }
                        continue;
                    }
                    const BlockID c_id = light_cell_id_at( chunk.cells, c );
                    if ( light_opaque_id( c_id ) ) {
                        continue;
                    }
                    cells++;
                    for ( int ch = 0; ch < 2; ch++ ) {
                        const int stored = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( cell ) : light_get_sky( cell );
                        int best = ch == LIGHT_CHANNEL_BLOCK ? block_definition_get_definition( c_id )->emits_light * LIGHT_RAW_PER_LEVEL : 0;
                        bool complete = true;
                        for ( int d = 0; d < LIGHT_STEP_COUNT; d++ ) {
                            const LightStepDir &sd = light_step_dirs[ d ];
                            glm::ivec3 sl;
                            const Chunk *src = owner_of( c - sd.d, sl );
                            if ( !src ) {
                                complete = false;
                                continue;
                            }
                            const unsigned short scell = light_read_cell( *src, light_index( sl.x, sl.y, sl.z ) );
                            const int sv = ch == LIGHT_CHANNEL_BLOCK ? light_get_block( scell ) : light_get_sky( scell );
                            if ( sv <= 0 ) {
                                continue;
                            }
                            const bool s_floor = block_definition_get_definition( light_cell_id_at( src->cells, sl ) )->blocks_light_down;
                            if ( s_floor && sd.d.y < 0 ) {
                                continue;
                            }
                            if ( sd.ngates && !light_diag_open( src->cells, sl, sd, s_floor ) ) {
                                continue;
                            }
                            best = std::max( best, light_offered( sv, ch, d, c_id ) );
                        }
                        const bool is_under = stored < best;
                        const bool is_over = complete && stored > best && stored > 0;
                        if ( !is_under && !is_over ) {
                            continue;
                        }
                        ( is_under ? under : over )[ ch ]++;
                        if ( logged < 20 ) {
                            logged++;
                            const glm::ivec3 w = chunk.chunk_pos * CHUNK_SIZE_I + c;
                            pr_debug( "Light verify: %s-lit %s at (%d,%d,%d) stored=%d best=%d", is_under ? "under" : "over", ch == LIGHT_CHANNEL_SKY ? "sky" : "block",
                                      w.x, w.y, w.z, stored, best );
                        }
                    }
                }
            }
        }
    }
    pr_debug( "Light verify: %lld cells, under sky=%lld block=%lld, over sky=%lld block=%lld, bad mirrors=%lld", cells, under[ LIGHT_CHANNEL_SKY ],
              under[ LIGHT_CHANNEL_BLOCK ], over[ LIGHT_CHANNEL_SKY ], over[ LIGHT_CHANNEL_BLOCK ], mirror_bad );
#if ( defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) ) && !defined( REPGAME_GLES )
    // GPU side: read each light texture back and compare with a fresh
    // full-volume fill of the current field — catches lost/stale uploads
    // that leave the screen dark over a correct field.
    const bool mipmaps = light_mipmaps_enabled( );
    const int bpp = mipmaps ? 4 : 2;
    const glm::ivec3 full( CHUNK_SIZE_INTERNAL_X, CHUNK_SIZE_INTERNAL_Y, CHUNK_SIZE_INTERNAL_Z );
    std::vector<unsigned char> want( static_cast<size_t>( CHUNK_BLOCK_SIZE ) * bpp ), got( want.size( ) );
    long long tex_checked = 0, tex_bad_chunks = 0, tex_bad_texels = 0, tex_mode_pending = 0;
    int tex_logged = 0;
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        Chunk &chunk = this->chunkArray[ i ];
        if ( !settled( &chunk ) || chunk.light.texture == 0 ) {
            continue;
        }
        const bool self_smooth = chunk.light.volume != nullptr && light_smooth_here( this, chunk.chunk_pos );
        if ( chunk.light.tex_mode.load( std::memory_order_relaxed ) != ( self_smooth ? 2 : 1 ) ) {
            tex_mode_pending++;
            continue;
        }
        tex_checked++;
        this->light_fill_upload_box( chunk, glm::ivec3( 0 ), full, mipmaps, self_smooth, want.data( ) );
        glActiveTexture( GL_TEXTURE0 + light_texture_unit( ) );
        glBindTexture( GL_TEXTURE_3D, chunk.light.texture );
        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
        glGetTexImage( GL_TEXTURE_3D, 0, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, got.data( ) );
        glPixelStorei( GL_PACK_ALIGNMENT, 4 );
        long long bad = 0;
        for ( size_t t = 0; t < want.size( ); t += bpp ) {
            if ( std::memcmp( &want[ t ], &got[ t ], bpp ) == 0 ) {
                continue;
            }
            bad++;
            if ( tex_logged < 20 ) {
                tex_logged++;
                const int idx = static_cast<int>( t / bpp );
                const glm::ivec3 l( idx % CHUNK_SIZE_INTERNAL_X - 1, ( idx / CHUNK_SIZE_INTERNAL_X ) % CHUNK_SIZE_INTERNAL_Y - 1,
                                    idx / ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y ) - 1 );
                const glm::ivec3 w = chunk.chunk_pos * CHUNK_SIZE_I + l;
                // Owner's own texel for the same cell (halo texels must match it).
                int owner_tex = -1, owner_mode = -1, owner_smooth = -1;
                const Chunk *owner = this->get_chunk( light_chunk_pos_of( w ) );
                if ( owner && owner != &chunk && owner->light.texture != 0 ) {
                    std::vector<unsigned char> ob( want.size( ) );
                    glBindTexture( GL_TEXTURE_3D, owner->light.texture );
                    glPixelStorei( GL_PACK_ALIGNMENT, 1 );
                    glGetTexImage( GL_TEXTURE_3D, 0, mipmaps ? GL_RGBA : GL_RG, GL_UNSIGNED_BYTE, ob.data( ) );
                    glPixelStorei( GL_PACK_ALIGNMENT, 4 );
                    glBindTexture( GL_TEXTURE_3D, chunk.light.texture );
                    const glm::ivec3 ol = w - owner->chunk_pos * CHUNK_SIZE_I + glm::ivec3( 1 );
                    owner_tex = ob[ static_cast<size_t>( ( ol.z * CHUNK_SIZE_INTERNAL_Y + ol.y ) * CHUNK_SIZE_INTERNAL_X + ol.x ) * bpp ];
                    owner_mode = owner->light.tex_mode.load( std::memory_order_relaxed );
                    owner_smooth = owner->light.volume != nullptr && light_smooth_here( this, owner->chunk_pos );
                }
                pr_debug( "Light verify: stale texel chunk (%d,%d,%d) mode=%d at (%d,%d,%d) want=%d,%d,%d,%d got=%d,%d,%d,%d owner_tex=%d owner_mode=%d owner_smooth=%d",
                          chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z, chunk.light.tex_mode.load( std::memory_order_relaxed ), w.x, w.y, w.z, want[ t ],
                          want[ t + 1 ], bpp > 2 ? want[ t + 2 ] : 0, bpp > 2 ? want[ t + 3 ] : 0, got[ t ], got[ t + 1 ], bpp > 2 ? got[ t + 2 ] : 0,
                          bpp > 2 ? got[ t + 3 ] : 0, owner_tex, owner_mode, owner_smooth );
            }
        }
        if ( bad ) {
            tex_bad_chunks++;
            tex_bad_texels += bad;
        }
    }
    glBindTexture( GL_TEXTURE_3D, 0 );
    pr_debug( "Light verify: textures checked=%lld stale chunks=%lld stale texels=%lld mode-flip pending=%lld", tex_checked, tex_bad_chunks, tex_bad_texels,
              tex_mode_pending );
#endif
}

