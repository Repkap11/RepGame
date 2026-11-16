// Nether fortresses: deterministic multi-section complexes stamped over the
// nether cavern band. Same contract as every StructureGen pass — a pure
// function of world coords: each chunk re-derives every fortress whose
// footprint could overlap it and writes only the cells it owns (+/-1 halo).
//
// Layout: a two-storey central keep (lower hall + upper room joined by a
// stepped stair) plus 4-5 satellite halls scattered a couple hundred
// blocks out. 1-2 junction halls sit closer in and share one connector
// back to the keep; satellites on a junction's bearing route through it,
// so corridors converge inside a real intersection room rather than
// fanning straight out of the keep, and occasional chain links cross
// between satellites. Bridge arms random-walk out of keep and satellite
// walls, ending in widened platforms that often carry a hollow tower
// with a chest and an inert MOB_SPAWNER block. Footprints spill past
// cell edges, so neighbouring complexes overlap and merge; supports
// drop from decks to the cavern floor or through the lava sea to its
// bed. Carve passes open interiors and tunnel bridge headroom through
// netherrack.

#include <random>
#include <string.h>

#include "common/structure_gen.hpp"
#include "common/chest.hpp"
#include "common/dimension.hpp"
#include "common/perlin_noise.hpp"
#include "common/constants.hpp"
#include "common/block_definitions.hpp"
#include "common/item_ids_gen.hpp"
#include "common/map_gen.hpp"
#include "common/tree_shapes.hpp"

#define MG_QUAL static inline
#define MAP_GEN_PERLIN2D( x, z, f, d, s ) perlin_noise2d( x, z, f, d, s )
#define MAP_GEN_PERLIN3D( x, y, z, f, d, s ) perlin_noise3d( x, y, z, f, d, s )
#include "common/map_gen_nether.hpp"

// sg_* salt budget: 100-139. structure_gen.cpp uses 1-95; tree_shapes.hpp
// uses 11-24, 58 — never reuse those.

// ---------------------------------------------------------------------------
// Grid + anchor. One fortress roll per SGF_CELL world cell; the anchor sits
// anywhere in the cell minus a margin, so the footprint (anchor +/- EXTENT)
// spills past cell edges — a chunk must enumerate every cell whose reachable
// interval overlaps its box, not just the cells it touches.
// ---------------------------------------------------------------------------

#define SGF_CELL 384        // world grid cell holding at most one fortress
#define SGF_EXTENT 360      // hard clip: no piece reaches past anchor +/- this
#define SGF_MARGIN 48       // anchor stays inside cell + [MARGIN, CELL-MARGIN)
#define SGF_DECK_MIN_LY 94  // lowest deck height, band-local y (lava sea = 60)
#define SGF_DECK_SPAN_LY 16 // narrow band [94, 110): mean ~ly 102 ≈ world -5850
#define SGF_FREQ 0.90f      // ~one complex per cell — near-continuous sprawl
#define SGF_PILLAR_MAX 160  // longest support column probe below a deck
#define SGF_TOP_RISE 48     // highest a piece top reaches above the deck
#define SGF_ANCHOR_TRIES 8  // anchor candidates per cell; most open wins

#define SGF_MAX_PIECES 320 // worst: 5 satellites + 2 junctions, each keep + ~34-step connector + arm
#define SGF_MAX_SLOTS 32
#define SGF_MAX_LAYOUTS 16 // cells whose footprint can reach one chunk

// Piece kinds. Every piece is an axis-aligned world-space bbox; aux carries
// a world direction where noted.
enum {
    SGF_BRIDGE,   // open walkway: deck row at y0; carve 3x3 channel above
    SGF_PLATFORM, // widened node deck; parapet rim with a gap on the aux side
    SGF_KEEP,     // enclosed hall: floor + walls + roof; interior carved
    SGF_TOWER,    // enclosed tower on a platform; door slit on the aux face
    SGF_STAIR,    // stepped run inside the keep (treads + floor-hole carves)
};

// World direction ids stored in aux / slot facing.
enum {
    SGF_DIR_PX,
    SGF_DIR_NX,
    SGF_DIR_PZ,
    SGF_DIR_NZ,
};

typedef struct {
    int kind;
    int x0, y0, z0, x1, y1, z1;
    int aux;
} SGFPiece;

typedef struct {
    int x, y, z, facing;
} SGFSlot;

typedef struct {
    int n_pieces;
    SGFPiece pieces[ SGF_MAX_PIECES ];
    int n_chests;
    SGFSlot chests[ SGF_MAX_SLOTS ];
    int n_spawners;
    glm::ivec3 spawners[ SGF_MAX_SLOTS ];
} SGFLayout;

typedef struct {
    int cx, cz;  // grid cell
    int ax, az;  // world x/z anchor (keep centre)
    int deck_ly; // band-local deck y
    int facing;  // 0..3 quarter-turns applied to the canonical layout
} SGFAnchor;

static int sgf_deck_y( const SGFAnchor &a ) {
    return DIM_BY_MIN_N + a.deck_ly;
}

// Per-cell existence + anchor. Only rolls the cheap fields — enough for the
// chunk-pass bbox reject; the full layout decode happens per overlapping
// chunk. The anchor picks the most open of SGF_ANCHOR_TRIES candidates —
// open air column (roof base minus floor), penalizing floors above the
// lava sea — so fortresses strongly favour open cavern and lava basins
// over spots embedded in the cavern mass.
static int sgf_for_cell( const int cx, const int cz, SGFAnchor *a ) {
    if ( sg_roll( cx, cz, 100 ) >= SGF_FREQ ) {
        return 0;
    }
    a->cx = cx;
    a->cz = cz;
    float best_score = -1e30f;
    float best_floor = 1e30f;
    a->ax = 0;
    a->az = 0;
    for ( int k = 0; k < SGF_ANCHOR_TRIES; k++ ) {
        const int x = cx * SGF_CELL + SGF_MARGIN + (int)( sg_roll( cx, cz, 102 + k * 2 ) * (float)( SGF_CELL - 2 * SGF_MARGIN ) );
        const int z = cz * SGF_CELL + SGF_MARGIN + (int)( sg_roll( cx, cz, 103 + k * 2 ) * (float)( SGF_CELL - 2 * SGF_MARGIN ) );
        const float f = mgn_floor_height( x, z );
        const float open = mgn_roof_base( x, z ) - f - 0.5f * ( f > (float)MGN_LAVA_SEA ? f - (float)MGN_LAVA_SEA : 0.0f );
        if ( open > best_score ) {
            best_score = open;
            best_floor = f;
            a->ax = x;
            a->az = z;
        }
    }
    // Require real open air at the anchor — a keep embedded in the cavern
    // wall is invisible from outside and reads as rubble, not a fortress.
    if ( mgn_roof_base( a->ax, a->az ) - best_floor < 45.0f ) {
        return 0;
    }
    // Deck rolls inside a narrow band around ly 102 (~world -5850) so
    // complexes perch at mid-cavern height over open floors and lava
    // basins alike, rather than hugging the sea.
    a->deck_ly = SGF_DECK_MIN_LY + (int)( sg_roll( cx, cz, 130 ) * (float)SGF_DECK_SPAN_LY );
    a->facing = (int)( sg_roll( cx, cz, 131 ) * 4.0f );
    // Never let the deck sit below the floor — that buries the keep
    // inside the hillside. Floors above ~ly 105 are too tall for the
    // band and reject.
    if ( a->deck_ly < best_floor + 4.0f ) {
        return 0;
    }
    // Keep clear of the roof mass too.
    if ( mgn_roof_base( a->ax, a->az ) - (float)a->deck_ly < 30.0f ) {
        return 0;
    }
    return 1;
}

int StructureGen::fortress_cell_exists( const int cx, const int cz, glm::ivec3 *anchor, int *deck_y ) {
    SGFAnchor a;
    if ( !sgf_for_cell( cx, cz, &a ) ) {
        return 0;
    }
    if ( anchor != nullptr ) {
        *anchor = glm::ivec3( a.ax, 0, a.az );
    }
    if ( deck_y != nullptr ) {
        *deck_y = sgf_deck_y( a );
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Layout decode. Pieces are generated in a canonical orientation (keep front
// faces canonical -x) around the anchor, then every coord/direction is
// rotated by a.facing. Integer math only; the piece list is what every chunk
// re-derives identically.
// ---------------------------------------------------------------------------

// facing: 0 = canonical; 1 = canonical +x -> world +z; 2 = +x -> -x; 3 = +x -> -z.
static glm::ivec2 sgf_rot( const int dx, const int dz, const int facing ) {
    switch ( facing ) {
        case 1:
            return glm::ivec2( -dz, dx );
        case 2:
            return glm::ivec2( -dx, -dz );
        case 3:
            return glm::ivec2( dz, -dx );
        default:
            return glm::ivec2( dx, dz );
    }
}

static int sgf_dir_delta( const int dir, int *dx, int *dz ) {
    switch ( dir ) {
        case SGF_DIR_PX:
            *dx = 1;
            *dz = 0;
            return 1;
        case SGF_DIR_NX:
            *dx = -1;
            *dz = 0;
            return 1;
        case SGF_DIR_PZ:
            *dx = 0;
            *dz = 1;
            return 1;
        case SGF_DIR_NZ:
            *dx = 0;
            *dz = -1;
            return 1;
    }
    return 0;
}

static int sgf_dir_id( const int dx, const int dz ) {
    if ( dx > 0 ) return SGF_DIR_PX;
    if ( dx < 0 ) return SGF_DIR_NX;
    if ( dz > 0 ) return SGF_DIR_PZ;
    return SGF_DIR_NZ;
}

static int sgf_rot_dir( const int dir, const int facing ) {
    int dx, dz;
    if ( !sgf_dir_delta( dir, &dx, &dz ) ) {
        return dir;
    }
    const glm::ivec2 r = sgf_rot( dx, dz, facing );
    return sgf_dir_id( r.x, r.y );
}

static void sgf_push( SGFLayout &L, const SGFAnchor &a, const int kind,
                      int x0, const int y0, int z0, int x1, const int y1, int z1, int aux ) {
    if ( L.n_pieces >= SGF_MAX_PIECES ) {
        return;
    }
    // Clip to the extent region in canonical space (pre-rotation, same
    // bounds) so the enumeration contract always holds.
    const int lo_x = a.ax - SGF_EXTENT, hi_x = a.ax + SGF_EXTENT;
    const int lo_z = a.az - SGF_EXTENT, hi_z = a.az + SGF_EXTENT;
    if ( x1 < lo_x || x0 > hi_x || z1 < lo_z || z0 > hi_z ) {
        return;
    }
    if ( x0 < lo_x ) x0 = lo_x;
    if ( x1 > hi_x ) x1 = hi_x;
    if ( z0 < lo_z ) z0 = lo_z;
    if ( z1 > hi_z ) z1 = hi_z;
    const glm::ivec2 p0 = sgf_rot( x0 - a.ax, z0 - a.az, a.facing );
    const glm::ivec2 p1 = sgf_rot( x1 - a.ax, z1 - a.az, a.facing );
    SGFPiece &p = L.pieces[ L.n_pieces++ ];
    p.kind = kind;
    p.x0 = a.ax + ( p0.x < p1.x ? p0.x : p1.x );
    p.x1 = a.ax + ( p0.x < p1.x ? p1.x : p0.x );
    p.y0 = y0;
    p.y1 = y1;
    p.z0 = a.az + ( p0.y < p1.y ? p0.y : p1.y );
    p.z1 = a.az + ( p0.y < p1.y ? p1.y : p0.y );
    p.aux = ( kind == SGF_PLATFORM || kind == SGF_TOWER || kind == SGF_STAIR ) ? sgf_rot_dir( aux, a.facing ) : aux;
}

static void sgf_push_chest( SGFLayout &L, const SGFAnchor &a, const int x, const int y, const int z, const int facing ) {
    if ( L.n_chests >= SGF_MAX_SLOTS ) {
        return;
    }
    // Same extent contract as pieces: a slot outside the declared footprint
    // would write into cells the enumeration doesn't cover — a lone chest a
    // neighbouring chunk never derives.
    if ( x < a.ax - SGF_EXTENT || x > a.ax + SGF_EXTENT || z < a.az - SGF_EXTENT || z > a.az + SGF_EXTENT ) {
        return;
    }
    const glm::ivec2 p = sgf_rot( x - a.ax, z - a.az, a.facing );
    SGFSlot &s = L.chests[ L.n_chests++ ];
    s.x = a.ax + p.x;
    s.y = y;
    s.z = a.az + p.y;
    s.facing = sgf_rot_dir( facing, a.facing );
}

static void sgf_push_spawner( SGFLayout &L, const SGFAnchor &a, const int x, const int y, const int z ) {
    if ( L.n_spawners >= SGF_MAX_SLOTS ) {
        return;
    }
    if ( x < a.ax - SGF_EXTENT || x > a.ax + SGF_EXTENT || z < a.az - SGF_EXTENT || z > a.az + SGF_EXTENT ) {
        return;
    }
    const glm::ivec2 p = sgf_rot( x - a.ax, z - a.az, a.facing );
    L.spawners[ L.n_spawners++ ] = glm::ivec3( a.ax + p.x, y, a.az + p.y );
}

// One bridge arm: a random walk of straight 7-wide deck segments starting at
// a keep wall, ending in a platform (which may carry a tower) or a bare end.
static void sgf_arm( SGFLayout &L, const SGFAnchor &a, std::mt19937_64 &rng, const int deck,
                     int wx, int wz, int dir ) {
    auto rr = [ & ]( const int lo, const int hi ) {
        return lo + (int)( rng( ) % (uint64_t)( hi - lo + 1 ) );
    };
    int dx = 0, dz = 0;
    sgf_dir_delta( dir, &dx, &dz );
    const int n_seg = 2 + rr( 0, 3 );
    for ( int s = 0; s < n_seg; s++ ) {
        const int len = rr( 14, 28 );
        const int ex = wx + dx * len, ez = wz + dz * len;
        // Deck bbox with the 7-wide span already applied (clip inside
        // sgf_push keeps everything inside the extent).
        int bx0 = wx < ex ? wx : ex, bx1 = wx < ex ? ex : wx;
        int bz0 = wz < ez ? wz : ez, bz1 = wz < ez ? ez : wz;
        if ( dx != 0 ) {
            bz0 -= 3;
            bz1 += 3;
        } else {
            bx0 -= 3;
            bx1 += 3;
        }
        sgf_push( L, a, SGF_BRIDGE, bx0, deck, bz0, bx1, deck, bz1, 0 );
        wx = ex;
        wz = ez;
        if ( s + 1 < n_seg && rr( 0, 1 ) ) {
            // Jog sideways: the next segment starts from this end cell.
            if ( dx != 0 ) {
                dir = rr( 0, 1 ) ? SGF_DIR_PZ : SGF_DIR_NZ;
                dz = dir == SGF_DIR_PZ ? 1 : -1;
                dx = 0;
            } else {
                dir = rr( 0, 1 ) ? SGF_DIR_PX : SGF_DIR_NX;
                dx = dir == SGF_DIR_PX ? 1 : -1;
                dz = 0;
            }
        }
    }
    if ( rr( 0, 99 ) >= 65 ) {
        return;
    }
    // Platform: 15x15 deck centred on the arm end; aux = the side the arm
    // arrived from (parapet gap + tower door face).
    const int entry = sgf_dir_id( -dx, -dz );
    sgf_push( L, a, SGF_PLATFORM, wx - 7, deck, wz - 7, wx + 7, deck, wz + 7, entry );
    if ( rr( 0, 99 ) >= 55 ) {
        return;
    }
    // Tower: 11x11 hollow keep on the platform, same door side.
    sgf_push( L, a, SGF_TOWER, wx - 5, deck, wz - 5, wx + 5, deck + 14, wz + 5, entry );
    sgf_push_chest( L, a, wx - 3, deck + 1, wz - 3, SGF_DIR_PX );
    sgf_push_spawner( L, a, wx + 3, deck + 1, wz + 3 );
}

// One hall section: lower hall (6 rows: floor, 4 interior, roof) plus a
// ground chest, and when `upper` an inset upper room over the canonical +x
// two-thirds joined by a stepped stair (with its own chest and spawner).
// Upper pieces are pushed with aux 1 so the support pass skips their floor.
static void sgf_emit_keep( SGFLayout &L, const SGFAnchor &a, const int kx, const int kz,
                          const int kd, const int kw, const int kdeck, const int upper ) {
    sgf_push( L, a, SGF_KEEP, kx - kd, kdeck, kz - kw, kx + kd, kdeck + 5, kz + kw, 0 );
    sgf_push_chest( L, a, kx - kd + 2, kdeck + 1, kz - kw + 2, SGF_DIR_PX );
    if ( !upper ) {
        return;
    }
    sgf_push( L, a, SGF_KEEP, kx - kd + 3, kdeck + 6, kz - kw + 2, kx + kd, kdeck + 11, kz + kw - 2, 1 );
    sgf_push( L, a, SGF_STAIR, kx - 3, kdeck + 1, kz + kw - 5, kx + 4, kdeck + 6, kz + kw - 3, SGF_DIR_PX );
    sgf_push_chest( L, a, kx + kd - 2, kdeck + 7, kz + kw - 4, SGF_DIR_NX );
    sgf_push_spawner( L, a, kx + kd - 4, kdeck + 7, kz - kw + 5 );
}

// Stepped deck linking two section centres: an L path — x leg first, then
// z — emitted as short BRIDGE pieces whose y eases between the two decks by
// at most ~1 per step. Where a leg crosses a hall wall the bridge's own
// carve channel punches the doorway.
static void sgf_connector( SGFLayout &L, const SGFAnchor &a,
                           int x0, int z0, const int x1, const int z1, const int ya, const int yb ) {
    const int lx = abs( x1 - x0 ), lz = abs( z1 - z0 );
    const int total = lx + lz;
    if ( total == 0 ) {
        return;
    }
    int walked = 0;
    for ( int leg = 0; leg < 2; leg++ ) {
        const int len = leg == 0 ? lx : lz;
        const int sgn = leg == 0 ? ( x1 > x0 ? 1 : -1 ) : ( z1 > z0 ? 1 : -1 );
        int done = 0;
        while ( done < len ) {
            const int step = len - done < 12 ? len - done : 12;
            const int mid = walked + done + step / 2;
            const int y = ya + (int)( ( (int64_t)( yb - ya ) * mid + total / 2 ) / total );
            int bx0, bx1, bz0, bz1;
            if ( leg == 0 ) {
                const int ex = x0 + sgn * step;
                bx0 = x0 < ex ? x0 : ex;
                bx1 = x0 < ex ? ex : x0;
                bz0 = z0 - 3;
                bz1 = z0 + 3;
                x0 = ex;
            } else {
                const int ez = z0 + sgn * step;
                bz0 = z0 < ez ? z0 : ez;
                bz1 = z0 < ez ? ez : z0;
                bx0 = x0 - 3;
                bx1 = x0 + 3;
                z0 = ez;
            }
            sgf_push( L, a, SGF_BRIDGE, bx0, y, bz0, bx1, y, bz1, 0 );
            done += step;
        }
        walked += len;
    }
}

static void sgf_decode( const SGFAnchor &a, SGFLayout &L ) {
    memset( &L, 0, sizeof( L ) );
    std::mt19937_64 rng( (uint64_t)sg_hash( a.cx, 0, a.cz, 101 ) );
    auto rr = [ & ]( const int lo, const int hi ) {
        return lo + (int)( rng( ) % (uint64_t)( hi - lo + 1 ) );
    };
    const int deck = sgf_deck_y( a );
    const int ax = a.ax, az = a.az;

    const int kw = 11 + rr( 0, 1 ) * 2; // half-width along z: 11 or 13
    const int kd = 14 + rr( 0, 2 ) * 2; // half-depth along x: 14, 16 or 18
    sgf_emit_keep( L, a, ax, az, kd, kw, deck, 1 );

    // Bridge arms: the front (canonical -x) always exists; sides ~50%,
    // back ~33%. Arms start on the keep wall face so their carve channel
    // punches the doorway.
    sgf_arm( L, a, rng, deck, ax - kd, az, SGF_DIR_NX );
    if ( rr( 0, 1 ) ) {
        sgf_arm( L, a, rng, deck, ax, az + kw, SGF_DIR_PZ );
    }
    if ( rr( 0, 1 ) ) {
        sgf_arm( L, a, rng, deck, ax, az - kw, SGF_DIR_NZ );
    }
    if ( rr( 0, 2 ) == 0 ) {
        sgf_arm( L, a, rng, deck, ax + kd, az, SGF_DIR_PX );
    }

    // Section ring: junction halls and satellites both sit on the eight
    // compass points around the keep. Stride 3 through the direction ring
    // spreads the picks so halls don't stack on the same side.
    static const int dirs[ 8 ][ 2 ] = {
        { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 },
    };
    const int d0 = rr( 0, 7 );

    // Junction halls: 1-2 shared nodes on the ring, closer in than the
    // satellites. Each is a small hall on its own deck level linked back
    // to the keep — satellites on a matching bearing route through one,
    // so corridors converge at a real intersection room.
    const int n_jun = 1 + rr( 0, 1 );
    int jx[ 2 ], jz[ 2 ], jdeck[ 2 ], jdir[ 2 ];
    for ( int j = 0; j < n_jun; j++ ) {
        const int dj = ( d0 + 2 + j * 4 ) & 7;
        jdir[ j ] = dj;
        const int dist = 70 + rr( 0, 50 );
        jx[ j ] = ax + dirs[ dj ][ 0 ] * dist;
        jz[ j ] = az + dirs[ dj ][ 1 ] * dist;
        jdeck[ j ] = deck + 4 * ( rr( 0, 2 ) - 1 );
        sgf_emit_keep( L, a, jx[ j ], jz[ j ], 7 + rr( 0, 2 ), 7 + rr( 0, 2 ), jdeck[ j ], 0 );
        sgf_connector( L, a, jx[ j ], jz[ j ], ax, az, jdeck[ j ], deck );
    }

    // Satellite sections: 4-5 halls on the ring, each on its own deck
    // level. A satellite on a junction's bearing links through the
    // junction (the corridors meet there); otherwise it occasionally
    // chains to the previous satellite — a crossing corridor — or spokes
    // straight to the keep.
    const int n_sat = 4 + rr( 0, 2 );
    int psx = 0, psz = 0, pdeck = 0;
    for ( int i = 0; i < n_sat; i++ ) {
        const int di = ( d0 + i * 3 ) & 7;
        const int dxx = dirs[ di ][ 0 ], dzz = dirs[ di ][ 1 ];
        const int dist = 90 + rr( 0, 110 );
        const int sx = ax + dxx * dist, sz = az + dzz * dist;
        const int sdeck = deck + 4 * ( rr( 0, 2 ) - 1 ); // deck-4, deck or deck+4
        const int skw = 9 + rr( 0, 3 ), skd = 11 + rr( 0, 4 );
        sgf_emit_keep( L, a, sx, sz, skd, skw, sdeck, rr( 0, 2 ) == 0 );
        int tx = ax, tz = az, tdeck = deck;
        for ( int j = 0; j < n_jun; j++ ) {
            const int ddj = ( di - jdir[ j ] + 8 ) & 7;
            if ( ddj <= 1 || ddj >= 7 ) {
                tx = jx[ j ];
                tz = jz[ j ];
                tdeck = jdeck[ j ];
            }
        }
        if ( tx == ax && tz == az && i > 0 && rr( 0, 99 ) < 25 &&
             abs( sx - psx ) + abs( sz - psz ) <= 300 ) {
            tx = psx;
            tz = psz;
            tdeck = pdeck;
        }
        sgf_connector( L, a, sx, sz, tx, tz, sdeck, tdeck );
        psx = sx;
        psz = sz;
        pdeck = sdeck;
        if ( rr( 0, 99 ) < 40 ) {
            // Extra arm off the satellite's outer face.
            const int dir = dxx != 0 ? ( dxx > 0 ? SGF_DIR_PX : SGF_DIR_NX ) : ( dzz > 0 ? SGF_DIR_PZ : SGF_DIR_NZ );
            sgf_arm( L, a, rng, sdeck, sx + dxx * skd, sz + dzz * skw, dir );
        }
    }
}

// ---------------------------------------------------------------------------
// Stamping. Three passes so the order is identical in every chunk that
// touches a piece: (1) solid shells, (2) carves, (3) details + supports.
// ---------------------------------------------------------------------------

// Wall material with weathering: mostly nether brick, sparse cracked and
// chiseled patches hashed on world coords so borders agree.
static BlockID sgf_brick( const int wx, const int wy, const int wz ) {
    const unsigned int h = sg_hash( wx, wy, wz, 120 ) & 31u;
    if ( h == 0u ) {
        return CHISELED_NETHER_BRICKS;
    }
    if ( h < 3u ) {
        return CRACKED_NETHER_BRICKS;
    }
    return NETHER_BRICK;
}

// Unconditional structure write — except bedrock, which structures never
// eat or carve.
static void sgf_put( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id == BEDROCK ) {
        return;
    }
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
}

static void sgf_put_state( Chunk &chunk, const int lx, const int ly, const int lz, const BlockID id, const int rotation ) {
    if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id == BEDROCK ) {
        return;
    }
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, (unsigned char)rotation, def->initial_redstone_power, id, 0 } );
}

static void sgf_carve( Chunk &chunk, const int lx, const int ly, const int lz ) {
    if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id == BEDROCK ) {
        return;
    }
    const Block *def = block_definition_get_definition( AIR );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { AIR, BLOCK_ROTATE_0, def->initial_redstone_power, AIR, 0 } );
}

// Clip a world bbox to the chunk's writable window (local coords, [-1, 62])
// and convert to locals. Returns 0 when nothing overlaps.
static int sgf_clip( const int cox, const int coy, const int coz,
                     const int x0, const int y0, const int z0, const int x1, const int y1, const int z1,
                     int *lx0, int *ly0, int *lz0, int *lx1, int *ly1, int *lz1 ) {
    *lx0 = x0 - cox > -1 ? x0 - cox : -1;
    *ly0 = y0 - coy > -1 ? y0 - coy : -1;
    *lz0 = z0 - coz > -1 ? z0 - coz : -1;
    *lx1 = x1 - cox < CHUNK_SIZE_X ? x1 - cox : CHUNK_SIZE_X;
    *ly1 = y1 - coy < CHUNK_SIZE_Y ? y1 - coy : CHUNK_SIZE_Y;
    *lz1 = z1 - coz < CHUNK_SIZE_Z ? z1 - coz : CHUNK_SIZE_Z;
    return *lx0 <= *lx1 && *ly0 <= *ly1 && *lz0 <= *lz1;
}

static void sgf_fill_y( Chunk &chunk, const int cox, const int coy, const int coz,
                        const int x0, const int y, const int z0, const int x1, const int z1 ) {
    int lx0, ly0, lz0, lx1, ly1, lz1;
    if ( !sgf_clip( cox, coy, coz, x0, y, z0, x1, y, z1, &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
        return;
    }
    for ( int lx = lx0; lx <= lx1; lx++ ) {
        for ( int lz = lz0; lz <= lz1; lz++ ) {
            sgf_put( chunk, lx, ly0, lz, sgf_brick( cox + lx, y, coz + lz ) );
        }
    }
}

static void sgf_shell( Chunk &chunk, const SGFPiece &p, const int cox, const int coy, const int coz ) {
    if ( p.kind == SGF_BRIDGE || p.kind == SGF_PLATFORM ) {
        sgf_fill_y( chunk, cox, coy, coz, p.x0, p.y0, p.z0, p.x1, p.z1 );
        return;
    }
    if ( p.kind != SGF_KEEP && p.kind != SGF_TOWER ) {
        return;
    }
    sgf_fill_y( chunk, cox, coy, coz, p.x0, p.y0, p.z0, p.x1, p.z1 ); // floor
    sgf_fill_y( chunk, cox, coy, coz, p.x0, p.y1, p.z0, p.x1, p.z1 ); // roof
    // Four walls between floor and roof.
    int lx0, ly0, lz0, lx1, ly1, lz1;
    if ( sgf_clip( cox, coy, coz, p.x0, p.y0 + 1, p.z0, p.x1, p.y1 - 1, p.z1, &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
        for ( int ly = ly0; ly <= ly1; ly++ ) {
            for ( int lx = lx0; lx <= lx1; lx++ ) {
                for ( int lz = lz0; lz <= lz1; lz++ ) {
                    const int wx = cox + lx, wz = coz + lz;
                    if ( wx == p.x0 || wx == p.x1 || wz == p.z0 || wz == p.z1 ) {
                        sgf_put( chunk, lx, ly, lz, sgf_brick( wx, coy + ly, wz ) );
                    }
                }
            }
        }
    }
}

// Is (wx, wy, wz) inside any piece of the given kind in the layout?
static int sgf_inside_kind( const SGFLayout &L, const int kind, const int wx, const int wy, const int wz ) {
    for ( int i = 0; i < L.n_pieces; i++ ) {
        const SGFPiece &p = L.pieces[ i ];
        if ( p.kind == kind && wx >= p.x0 && wx <= p.x1 && wy >= p.y0 && wy <= p.y1 && wz >= p.z0 && wz <= p.z1 ) {
            return 1;
        }
    }
    return 0;
}

static void sgf_carve_cell( Chunk &chunk, const int cox, const int coy, const int coz, const int wx, const int wy, const int wz ) {
    const int lx = wx - cox, ly = wy - coy, lz = wz - coz;
    if ( lx < -1 || lx > CHUNK_SIZE_X || ly < -1 || ly > CHUNK_SIZE_Y || lz < -1 || lz > CHUNK_SIZE_Z ) {
        return;
    }
    sgf_carve( chunk, lx, ly, lz );
}

static void sgf_carve_piece( Chunk &chunk, const SGFLayout &L, const SGFPiece &p,
                             const int cox, const int coy, const int coz ) {
    int lx0, ly0, lz0, lx1, ly1, lz1;
    if ( p.kind == SGF_KEEP || p.kind == SGF_TOWER ) {
        // Interior air: the bbox minus its shell.
        if ( sgf_clip( cox, coy, coz, p.x0 + 1, p.y0 + 1, p.z0 + 1, p.x1 - 1, p.y1 - 1, p.z1 - 1,
                       &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
            for ( int lx = lx0; lx <= lx1; lx++ ) {
                for ( int ly = ly0; ly <= ly1; ly++ ) {
                    for ( int lz = lz0; lz <= lz1; lz++ ) {
                        sgf_carve( chunk, lx, ly, lz );
                    }
                }
            }
        }
        // Arrow slits: one row of 1-cell openings on each wall face,
        // every 4th column at eye height.
        const int wy = p.y0 + 3;
        for ( int wx = p.x0 + 2; wx < p.x1; wx += 4 ) {
            sgf_carve_cell( chunk, cox, coy, coz, wx, wy, p.z0 );
            sgf_carve_cell( chunk, cox, coy, coz, wx, wy, p.z1 );
        }
        for ( int wz = p.z0 + 2; wz < p.z1; wz += 4 ) {
            sgf_carve_cell( chunk, cox, coy, coz, p.x0, wy, wz );
            sgf_carve_cell( chunk, cox, coy, coz, p.x1, wy, wz );
        }
        if ( p.kind == SGF_TOWER ) {
            // Door: 3 wide, 3 tall at the centre of the aux face.
            int dx = 0, dz = 0;
            sgf_dir_delta( p.aux, &dx, &dz );
            const int mx = ( p.x0 + p.x1 ) / 2, mz = ( p.z0 + p.z1 ) / 2;
            const int fx = dx != 0 ? ( dx > 0 ? p.x1 : p.x0 ) : mx;
            const int fz = dz != 0 ? ( dz > 0 ? p.z1 : p.z0 ) : mz;
            for ( int off = -1; off <= 1; off++ ) {
                for ( int dy = 1; dy <= 3; dy++ ) {
                    sgf_carve_cell( chunk, cox, coy, coz, fx + dz * off, p.y0 + dy, fz + dx * off );
                }
            }
        }
        return;
    }
    if ( p.kind == SGF_BRIDGE ) {
        // Walkway headroom channel: full length x inner 3 across, deck+1..3.
        // In open cavern it's already air; through netherrack it tunnels —
        // and where a segment overlaps the keep wall it punches the doorway.
        const int along_x = p.x1 - p.x0 >= p.z1 - p.z0;
        const int y0 = p.y0 + 1, y1 = p.y0 + 3;
        const int cx0 = along_x ? p.x0 : p.x0 + 1;
        const int cx1 = along_x ? p.x1 : p.x1 - 1;
        const int cz0 = along_x ? p.z0 + 1 : p.z0;
        const int cz1 = along_x ? p.z1 - 1 : p.z1;
        if ( sgf_clip( cox, coy, coz, cx0, y0, cz0, cx1, y1, cz1, &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
            for ( int lx = lx0; lx <= lx1; lx++ ) {
                for ( int ly = ly0; ly <= ly1; ly++ ) {
                    for ( int lz = lz0; lz <= lz1; lz++ ) {
                        sgf_carve( chunk, lx, ly, lz );
                    }
                }
            }
        }
        return;
    }
    if ( p.kind == SGF_PLATFORM ) {
        // Carve the deck's top channel inside the rim — except cells inside
        // a tower footprint (carving those would open the tower walls).
        if ( sgf_clip( cox, coy, coz, p.x0 + 1, p.y0 + 1, p.z0 + 1, p.x1 - 1, p.y0 + 3, p.z1 - 1,
                       &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
            for ( int lx = lx0; lx <= lx1; lx++ ) {
                for ( int ly = ly0; ly <= ly1; ly++ ) {
                    for ( int lz = lz0; lz <= lz1; lz++ ) {
                        if ( sgf_inside_kind( L, SGF_TOWER, cox + lx, coy + ly, coz + lz ) ) {
                            continue;
                        }
                        sgf_carve( chunk, lx, ly, lz );
                    }
                }
            }
        }
        return;
    }
    if ( p.kind == SGF_STAIR ) {
        // Floor holes for the run's headroom: wherever a tread's 3-cell
        // clearance reaches a slab row (the hall's roof at y1-1 or the
        // upper floor at y1), open it. Tread cells themselves are never
        // carved — treads sit below their headroom cells.
        int dx = 0, dz = 0;
        sgf_dir_delta( p.aux, &dx, &dz );
        // Low-end corner of the run (aux points uphill).
        const int sx = dx > 0 ? p.x0 : dx < 0 ? p.x1 : p.x0;
        const int sz = dz > 0 ? p.z0 : dz < 0 ? p.z1 : p.z0;
        for ( int i = 0; i <= p.y1 - p.y0; i++ ) {
            const int tx = sx + dx * i, tz = sz + dz * i;
            const int ty = p.y0 + i;
            for ( int hy = ty + 1; hy <= ty + 3; hy++ ) {
                if ( hy != p.y1 - 1 && hy != p.y1 ) {
                    continue;
                }
                if ( dx != 0 ) {
                    for ( int wz = p.z0; wz <= p.z1; wz++ ) {
                        sgf_carve_cell( chunk, cox, coy, coz, tx, hy, wz );
                    }
                } else {
                    for ( int wx = p.x0; wx <= p.x1; wx++ ) {
                        sgf_carve_cell( chunk, cox, coy, coz, wx, hy, tz );
                    }
                }
            }
        }
        return;
    }
}

// Support column under (wx, wz): fill from the first solid mapgen cell
// below y_top up to y_top, footprint `half` blocks each side. A column
// under a deck stops at lava or cavern rock; one buried in terrain writes
// nothing.
static void sgf_pillar( Chunk &chunk, const int cox, const int coy, const int coz,
                        const int wx, const int wz, const int y_top, const int half ) {
    // Cheap x/z reject before the mapgen probe — most pieces of the cells
    // enumerated for a chunk sit outside its writable window.
    if ( wx + half < cox - 1 || wx - half > cox + CHUNK_SIZE_X ||
         wz + half < coz - 1 || wz - half > coz + CHUNK_SIZE_Z ) {
        return;
    }
    int base = y_top - SGF_PILLAR_MAX;
    const int floor_min = DIM_BY_MIN_N + 2;
    if ( base < floor_min ) {
        base = floor_min;
    }
    int top_solid = -1;
    for ( int wy = y_top; wy >= base; wy-- ) {
        const BlockID b = MapGen::gen_block_id( wx, wy, wz );
        if ( b != AIR && b != LAVA ) {
            top_solid = wy;
            break;
        }
    }
    if ( top_solid < 0 ) {
        return;
    }
    for ( int dx = -half; dx <= half; dx++ ) {
        for ( int dz = -half; dz <= half; dz++ ) {
            for ( int wy = top_solid + 1; wy <= y_top; wy++ ) {
                sgf_put( chunk, wx + dx - cox, wy - coy, wz + dz - coz, sgf_brick( wx + dx, wy, wz + dz ) );
            }
        }
    }
}

// Walkway parapets: a fence at deck+1 on each outer edge cell — skipped
// where another piece's deck continues past that edge (jog joins and
// platform entries stay open).
static void sgf_parapet( Chunk &chunk, const SGFLayout &L, const SGFPiece &p,
                         const int cox, const int coy, const int coz ) {
    for ( int wx = p.x0; wx <= p.x1; wx++ ) {
        for ( int wz = p.z0; wz <= p.z1; wz++ ) {
            const bool edge_x0 = wx == p.x0, edge_x1 = wx == p.x1;
            const bool edge_z0 = wz == p.z0, edge_z1 = wz == p.z1;
            if ( !( edge_x0 || edge_x1 || edge_z0 || edge_z1 ) ) {
                continue;
            }
            // Edge cells inside an enclosed piece are walls/floors, not open
            // deck edge — a fence here would embed in a keep wall.
            if ( sgf_inside_kind( L, SGF_KEEP, wx, p.y0, wz ) || sgf_inside_kind( L, SGF_TOWER, wx, p.y0, wz ) ) {
                continue;
            }
            if ( p.kind == SGF_PLATFORM ) {
                // Gap on the entry side's middle stretch.
                int dx = 0, dz = 0;
                sgf_dir_delta( p.aux, &dx, &dz );
                const int mx = ( p.x0 + p.x1 ) / 2, mz = ( p.z0 + p.z1 ) / 2;
                const bool on_entry = ( dx > 0 && edge_x1 && wz >= mz - 1 && wz <= mz + 1 ) ||
                                      ( dx < 0 && edge_x0 && wz >= mz - 1 && wz <= mz + 1 ) ||
                                      ( dz > 0 && edge_z1 && wx >= mx - 1 && wx <= mx + 1 ) ||
                                      ( dz < 0 && edge_z0 && wx >= mx - 1 && wx <= mx + 1 );
                if ( on_entry ) {
                    continue;
                }
            } else {
                // Bridge: skip when the cell just outside an edge is covered
                // by another piece's deck (the joint stays open).
                bool join = false;
                if ( edge_x0 ) join |= sgf_inside_kind( L, SGF_BRIDGE, wx - 1, p.y0, wz ) || sgf_inside_kind( L, SGF_PLATFORM, wx - 1, p.y0, wz );
                if ( edge_x1 ) join |= sgf_inside_kind( L, SGF_BRIDGE, wx + 1, p.y0, wz ) || sgf_inside_kind( L, SGF_PLATFORM, wx + 1, p.y0, wz );
                if ( edge_z0 ) join |= sgf_inside_kind( L, SGF_BRIDGE, wx, p.y0, wz - 1 ) || sgf_inside_kind( L, SGF_PLATFORM, wx, p.y0, wz - 1 );
                if ( edge_z1 ) join |= sgf_inside_kind( L, SGF_BRIDGE, wx, p.y0, wz + 1 ) || sgf_inside_kind( L, SGF_PLATFORM, wx, p.y0, wz + 1 );
                if ( join ) {
                    continue;
                }
            }
            sgf_put( chunk, wx - cox, p.y0 + 1 - coy, wz - coz, NETHER_BRICK_FENCE );
        }
    }
}

static void sgf_detail( Chunk &chunk, const SGFLayout &L, const SGFPiece &p,
                        const int cox, const int coy, const int coz ) {
    if ( p.kind == SGF_STAIR ) {
        // Open-riser treads rising 1/block toward aux; the matching floor
        // holes were carved in pass 2.
        int dx = 0, dz = 0;
        sgf_dir_delta( p.aux, &dx, &dz );
        const int sx = dx > 0 ? p.x0 : dx < 0 ? p.x1 : p.x0;
        const int sz = dz > 0 ? p.z0 : dz < 0 ? p.z1 : p.z0;
        for ( int i = 0; i <= p.y1 - p.y0; i++ ) {
            const int tx = sx + dx * i, tz = sz + dz * i;
            if ( dx != 0 ) {
                for ( int wz = p.z0; wz <= p.z1; wz++ ) {
                    sgf_put( chunk, tx - cox, p.y0 + i - coy, wz - coz, NETHER_BRICK );
                }
            } else {
                for ( int wx = p.x0; wx <= p.x1; wx++ ) {
                    sgf_put( chunk, wx - cox, p.y0 + i - coy, tz - coz, NETHER_BRICK );
                }
            }
        }
    }
    if ( p.kind == SGF_BRIDGE || p.kind == SGF_PLATFORM ) {
        sgf_parapet( chunk, L, p, cox, coy, coz );
    }
    // Sparse shroomlight accents let interior rooms read in the dark, and a
    // fence ring on the roofline reads as battlements — skipped where
    // another enclosed piece covers that edge (the upper room over the
    // lower keep's roof).
    if ( p.kind == SGF_KEEP || p.kind == SGF_TOWER ) {
        int lx0, ly0, lz0, lx1, ly1, lz1;
        if ( sgf_clip( cox, coy, coz, p.x0 + 2, p.y1 - 1, p.z0 + 2, p.x1 - 2, p.y1 - 1, p.z1 - 2,
                       &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
            for ( int lx = lx0; lx <= lx1; lx++ ) {
                for ( int lz = lz0; lz <= lz1; lz++ ) {
                    const int wx = cox + lx, wz = coz + lz;
                    if ( ( sg_hash( wx, coy + ly0, wz, 122 ) & 255u ) < 6u ) {
                        sgf_put( chunk, lx, ly0, lz, SHROOMLIGHT );
                    }
                }
            }
        }
        if ( sgf_clip( cox, coy, coz, p.x0, p.y1 + 1, p.z0, p.x1, p.y1 + 1, p.z1,
                       &lx0, &ly0, &lz0, &lx1, &ly1, &lz1 ) ) {
            for ( int lx = lx0; lx <= lx1; lx++ ) {
                for ( int lz = lz0; lz <= lz1; lz++ ) {
                    const int wx = cox + lx, wz = coz + lz;
                    if ( wx != p.x0 && wx != p.x1 && wz != p.z0 && wz != p.z1 ) {
                        continue; // roofline edge cells only
                    }
                    if ( sgf_inside_kind( L, SGF_KEEP, wx, p.y1 + 1, wz ) ||
                         sgf_inside_kind( L, SGF_TOWER, wx, p.y1 + 1, wz ) ) {
                        continue;
                    }
                    sgf_put( chunk, lx, ly0, lz, NETHER_BRICK_FENCE );
                }
            }
        }
    }
    // Floor-level pieces get support columns down to terrain. An upper keep
    // floor sits on its lower hall — aux 1 excludes it — while satellites on
    // their own deck levels still carry pillars.
    if ( p.kind == SGF_KEEP && p.aux != 0 ) {
        return;
    }
    if ( p.kind == SGF_STAIR ) {
        return;
    }
    if ( p.kind == SGF_KEEP || p.kind == SGF_TOWER || p.kind == SGF_PLATFORM ) {
        sgf_pillar( chunk, cox, coy, coz, p.x0 + 1, p.z0 + 1, p.y0 - 1, 1 );
        sgf_pillar( chunk, cox, coy, coz, p.x1 - 1, p.z0 + 1, p.y0 - 1, 1 );
        sgf_pillar( chunk, cox, coy, coz, p.x0 + 1, p.z1 - 1, p.y0 - 1, 1 );
        sgf_pillar( chunk, cox, coy, coz, p.x1 - 1, p.z1 - 1, p.y0 - 1, 1 );
    }
    if ( p.kind == SGF_BRIDGE ) {
        // Spine supports at both ends and every 8th cell along the run —
        // 3x3 to carry the wider deck.
        const int mx = ( p.x0 + p.x1 ) / 2, mz = ( p.z0 + p.z1 ) / 2;
        for ( int wx = p.x0 + 3; wx <= p.x1 - 3; wx += 8 ) {
            sgf_pillar( chunk, cox, coy, coz, wx, mz, p.y0 - 1, 1 );
        }
        for ( int wz = p.z0 + 3; wz <= p.z1 - 3; wz += 8 ) {
            sgf_pillar( chunk, cox, coy, coz, mx, wz, p.y0 - 1, 1 );
        }
        sgf_pillar( chunk, cox, coy, coz, mx, p.z0 + 3, p.y0 - 1, 1 );
        sgf_pillar( chunk, cox, coy, coz, mx, p.z1 - 3, p.y0 - 1, 1 );
        sgf_pillar( chunk, cox, coy, coz, p.x0 + 3, mz, p.y0 - 1, 1 );
        sgf_pillar( chunk, cox, coy, coz, p.x1 - 3, mz, p.y0 - 1, 1 );
    }
}

static int sgf_floor_div( const int v ) {
    // Cells are positive-sized; v can be negative near the origin.
    int q = v / SGF_CELL;
    if ( v % SGF_CELL != 0 && v < 0 ) {
        q--;
    }
    return q;
}

// Cells whose fortress footprint can reach the world interval edge:
// cell c's reach is [c*CELL + MARGIN - EXTENT, (c+1)*CELL - MARGIN + EXTENT].
static int sgf_cells_lo( const int lo ) {
    return sgf_floor_div( lo - SGF_CELL + SGF_MARGIN - SGF_EXTENT );
}
static int sgf_cells_hi( const int hi ) {
    return sgf_floor_div( hi + SGF_EXTENT - SGF_MARGIN );
}

// Gather every fortress layout whose footprint reaches this chunk, then
// stamp shells -> carves -> details as global passes across all of them:
// a neighbouring complex's carve or parapet lands on top of this one's
// shell, so overlapping sections connect instead of walling each other off.
void StructureGen::place_nether_fortress( Chunk &chunk, const int cox, const int coy, const int coz ) {
    const int cx0 = sgf_cells_lo( cox - 1 ), cx1 = sgf_cells_hi( cox + CHUNK_SIZE_X );
    const int cz0 = sgf_cells_lo( coz - 1 ), cz1 = sgf_cells_hi( coz + CHUNK_SIZE_Z );
    SGFLayout layouts[ SGF_MAX_LAYOUTS ];
    int n_layouts = 0;
    for ( int cx = cx0; cx <= cx1; cx++ ) {
        for ( int cz = cz0; cz <= cz1; cz++ ) {
            SGFAnchor a;
            if ( n_layouts >= SGF_MAX_LAYOUTS || !sgf_for_cell( cx, cz, &a ) ) {
                continue;
            }
            const int deck = sgf_deck_y( a );
            if ( coy - 1 > deck + SGF_TOP_RISE || coy + CHUNK_SIZE_Y + 1 < deck - SGF_PILLAR_MAX ) {
                continue;
            }
            sgf_decode( a, layouts[ n_layouts++ ] );
        }
    }
    for ( int l = 0; l < n_layouts; l++ ) {
        for ( int i = 0; i < layouts[ l ].n_pieces; i++ ) {
            sgf_shell( chunk, layouts[ l ].pieces[ i ], cox, coy, coz );
        }
    }
    for ( int l = 0; l < n_layouts; l++ ) {
        for ( int i = 0; i < layouts[ l ].n_pieces; i++ ) {
            sgf_carve_piece( chunk, layouts[ l ], layouts[ l ].pieces[ i ], cox, coy, coz );
        }
    }
    static const int chest_rot[ 4 ] = { 1, 3, 0, 2 }; // front: +x -x +z -z
    for ( int l = 0; l < n_layouts; l++ ) {
        const SGFLayout &L = layouts[ l ];
        for ( int i = 0; i < L.n_pieces; i++ ) {
            sgf_detail( chunk, L, L.pieces[ i ], cox, coy, coz );
        }
        // Chest + spawner slots.
        for ( int i = 0; i < L.n_chests; i++ ) {
            const SGFSlot &s = L.chests[ i ];
            sgf_put_state( chunk, s.x - cox, s.y - coy, s.z - coz, CHEST, chest_rot[ s.facing & 3 ] );
        }
        for ( int i = 0; i < L.n_spawners; i++ ) {
            const glm::ivec3 &s = L.spawners[ i ];
            sgf_put( chunk, s.x - cox, s.y - coy, s.z - coz, MOB_SPAWNER );
        }
    }
}

// ---------------------------------------------------------------------------
// Generated loot. Chest sidecar files only ever hold player-touched chests;
// fortress chests roll their contents from the world position instead, so an
// unexplored chunk keeps re-deriving the same unopened chest. Runs after the
// chest sidecar load in Chunk::load_terrain so saved contents always win.
// ---------------------------------------------------------------------------

static ChestContents sgf_roll_loot( const int wx, const int wy, const int wz ) {
    static const struct {
        uint32_t item;
        int weight;
        int q0, q1;
    } table[] = {
        { GOLD_INGOT, 26, 2, 6 },
        { IRON_INGOT, 20, 1, 4 },
        { COAL, 20, 2, 8 },
        { BONE, 14, 1, 5 },
        { DIAMOND, 6, 1, 3 },
        { FLINT, 8, 1, 4 },
        { FLINT_AND_STEEL, 4, 1, 1 },
        { ARROW, 10, 4, 12 },
    };
    int total = 0;
    for ( const auto &t : table ) {
        total += t.weight;
    }
    ChestContents c;
    const int n = 3 + (int)( sg_hash( wx, wy, wz, 110 ) % 5 ); // 3..7 stacks
    for ( int i = 0; i < n; i++ ) {
        const unsigned int h = sg_hash( wx, wy, wz, 111 + i * 2 );
        int pick = (int)( h % (unsigned int)total );
        int t = 0;
        while ( pick >= table[ t ].weight ) {
            pick -= table[ t ].weight;
            t++;
        }
        // Linear probe for a free slot — deterministic across calls.
        int slot = (int)( ( h >> 8 ) % CHEST_SLOTS );
        while ( c.slots[ slot ].item_id != LAST_BLOCK_ID ) {
            slot = ( slot + 1 ) % CHEST_SLOTS;
        }
        c.slots[ slot ].item_id = table[ t ].item;
        c.slots[ slot ].quantity = table[ t ].q0 + (int)( ( h >> 16 ) % (unsigned int)( table[ t ].q1 - table[ t ].q0 + 1 ) );
    }
    return c;
}

void StructureGen::place_fortress_loot( Chunk &chunk ) {
    if ( chunk.cells == nullptr || dimension_of_chunk( chunk.chunk_pos ) != DIM_NETHER ) {
        return;
    }
    const glm::ivec3 origin = chunk.chunk_pos * CHUNK_SIZE_I;
    const int cox = origin.x, coy = origin.y, coz = origin.z;
    const int cx0 = sgf_cells_lo( cox );
    const int cx1 = sgf_cells_hi( cox + CHUNK_SIZE_X - 1 );
    const int cz0 = sgf_cells_lo( coz );
    const int cz1 = sgf_cells_hi( coz + CHUNK_SIZE_Z - 1 );
    for ( int cx = cx0; cx <= cx1; cx++ ) {
        for ( int cz = cz0; cz <= cz1; cz++ ) {
            SGFAnchor a;
            if ( !sgf_for_cell( cx, cz, &a ) ) {
                continue;
            }
            const int deck = sgf_deck_y( a );
            if ( coy > deck + SGF_TOP_RISE || coy + CHUNK_SIZE_Y - 1 < deck - 1 ) {
                continue;
            }
            SGFLayout L;
            sgf_decode( a, L );
            for ( int i = 0; i < L.n_chests; i++ ) {
                const SGFSlot &s = L.chests[ i ];
                const glm::ivec3 diff = glm::ivec3( s.x, s.y, s.z ) - origin;
                if ( diff.x < 0 || diff.x >= CHUNK_SIZE_X || diff.y < 0 || diff.y >= CHUNK_SIZE_Y || diff.z < 0 || diff.z >= CHUNK_SIZE_Z ) {
                    continue;
                }
                const int index = Chunk::get_index_from_coords( diff );
                // The slot only earns loot while the generated chest block
                // stands there (a player edit that removed it wins), and a
                // persisted sidecar entry always wins.
                if ( !block_is_chest( chunk.id_at( index ) ) || chunk.containers.chests.count( index ) ) {
                    continue;
                }
                chunk.containers.chests[ index ] = sgf_roll_loot( s.x, s.y, s.z );
            }
        }
    }
}
