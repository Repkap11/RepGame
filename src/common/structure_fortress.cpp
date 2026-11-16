// Nether fortresses: deterministic bridge networks and keeps stamped over
// the nether cavern band. Same contract as every StructureGen pass — a pure
// function of world coords: each chunk re-derives every fortress whose
// footprint could overlap it and writes only the cells it owns (+/-1 halo).
//
// Layout: a two-storey keep (lower hall + upper room joined by a stepped
// stair) anchored over the lowest-floored of a few candidate spots — lava
// basins and open cavern — with 2-4 bridge arms random-walking out of its
// walls: each arm a chain of 7-wide decked segments ending in a widened
// platform, over half of which carry a hollow tower with a chest and an
// inert MOB_SPAWNER block. Supports drop from decks to the cavern floor or
// through the lava sea to its bed. Carve passes open interiors and tunnel
// bridge headroom through netherrack, so embedded fortresses read like
// MC's.

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
// in the cell's middle half so the whole footprint stays inside its cell —
// a chunk only ever needs to check the cells its own box overlaps.
// ---------------------------------------------------------------------------

#define SGF_CELL 384        // world grid cell holding at most one fortress
#define SGF_EXTENT 152      // hard clip: no piece reaches past anchor +/- this
#define SGF_DECK_MIN_LY 66  // lowest deck height, band-local y (lava sea = 60)
#define SGF_DECK_SPAN_LY 44 // deck cap rolled inside [66, 110)
#define SGF_FREQ 0.20f      // ~1 fortress per 5 cells (~700x700 blocks)
#define SGF_PILLAR_MAX 160  // longest support column probe below a deck
#define SGF_TOP_RISE 40     // highest a piece top reaches above the deck
#define SGF_ANCHOR_TRIES 4  // anchor candidates per cell; lowest floor wins

#define SGF_MAX_PIECES 64
#define SGF_MAX_SLOTS 16

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
// chunk. The anchor picks the lowest-floored of SGF_ANCHOR_TRIES candidates,
// so fortresses favour open cavern and lava basins over embedded spots.
static int sgf_for_cell( const int cx, const int cz, SGFAnchor *a ) {
    if ( sg_roll( cx, cz, 100 ) >= SGF_FREQ ) {
        return 0;
    }
    a->cx = cx;
    a->cz = cz;
    float best_floor = 1e30f;
    a->ax = 0;
    a->az = 0;
    for ( int k = 0; k < SGF_ANCHOR_TRIES; k++ ) {
        const int x = cx * SGF_CELL + SGF_EXTENT + (int)( sg_roll( cx, cz, 102 + k * 2 ) * (float)( SGF_CELL - 2 * SGF_EXTENT ) );
        const int z = cz * SGF_CELL + SGF_EXTENT + (int)( sg_roll( cx, cz, 103 + k * 2 ) * (float)( SGF_CELL - 2 * SGF_EXTENT ) );
        const float f = mgn_floor_height( x, z );
        if ( f < best_floor ) {
            best_floor = f;
            a->ax = x;
            a->az = z;
        }
    }
    // Deck sits a small gap above the local floor — over a lava basin that
    // lands just over the sea — capped by a second roll in the open cavern
    // so high shelves still get embedded fortresses sometimes.
    a->deck_ly = (int)best_floor + 8 + (int)( sg_roll( cx, cz, 130 ) * 15.0f );
    const int cap = SGF_DECK_MIN_LY + (int)( sg_roll( cx, cz, 132 ) * (float)SGF_DECK_SPAN_LY );
    if ( a->deck_ly > cap ) {
        a->deck_ly = cap;
    }
    if ( a->deck_ly < SGF_DECK_MIN_LY ) {
        a->deck_ly = SGF_DECK_MIN_LY;
    }
    a->facing = (int)( sg_roll( cx, cz, 131 ) * 4.0f );
    // Sanity gates: keep clear of the roof mass, and don't waste the roll on
    // a keep that never breaches the surface.
    if ( mgn_roof_base( a->ax, a->az ) - (float)a->deck_ly < 30.0f ) {
        return 0;
    }
    if ( a->deck_ly + 16 < best_floor ) {
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

static void sgf_decode( const SGFAnchor &a, SGFLayout &L ) {
    memset( &L, 0, sizeof( L ) );
    std::mt19937_64 rng( (uint64_t)sg_hash( a.cx, 0, a.cz, 101 ) );
    auto rr = [ & ]( const int lo, const int hi ) {
        return lo + (int)( rng( ) % (uint64_t)( hi - lo + 1 ) );
    };
    const int deck = sgf_deck_y( a );
    const int ax = a.ax, az = a.az;

    // Keep: lower hall (8 rows: floor, 6 interior, roof) and a slightly
    // inset upper room over the canonical +x two-thirds, joined by a
    // stepped stair run inside the lower hall against the +z inner wall.
    const int kw = 11 + rr( 0, 1 ) * 2; // half-width along z: 11 or 13
    const int kd = 14 + rr( 0, 2 ) * 2; // half-depth along x: 14, 16 or 18
    sgf_push( L, a, SGF_KEEP, ax - kd, deck, az - kw, ax + kd, deck + 7, az + kw, 0 );
    sgf_push( L, a, SGF_KEEP, ax - kd + 3, deck + 8, az - kw + 2, ax + kd, deck + 15, az + kw - 2, 0 );
    sgf_push( L, a, SGF_STAIR, ax - 3, deck + 1, az + kw - 5, ax + 4, deck + 8, az + kw - 3, SGF_DIR_PX );

    // Loot: one chest per keep level plus an upper-room spawner; each
    // arm-end tower adds another of both.
    sgf_push_chest( L, a, ax - kd + 2, deck + 1, az - kw + 2, SGF_DIR_PX );
    sgf_push_chest( L, a, ax + kd - 2, deck + 9, az + kw - 4, SGF_DIR_NX );
    sgf_push_spawner( L, a, ax + kd - 4, deck + 9, az - kw + 5 );

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
        // clearance reaches a slab row (the keep's roof at deck+7 or the
        // upper floor at deck+8), open it. Tread cells themselves are
        // never carved — treads sit below their headroom cells.
        int dx = 0, dz = 0;
        sgf_dir_delta( p.aux, &dx, &dz );
        const int deck = p.y0 - 1;
        // Low-end corner of the run (aux points uphill).
        const int sx = dx > 0 ? p.x0 : dx < 0 ? p.x1 : p.x0;
        const int sz = dz > 0 ? p.z0 : dz < 0 ? p.z1 : p.z0;
        for ( int i = 0; i < 8; i++ ) {
            const int tx = sx + dx * i, tz = sz + dz * i;
            const int ty = p.y0 + i;
            for ( int hy = ty + 1; hy <= ty + 3; hy++ ) {
                if ( hy != deck + 7 && hy != deck + 8 ) {
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

static void sgf_detail( Chunk &chunk, const int deck, const SGFLayout &L, const SGFPiece &p,
                        const int cox, const int coy, const int coz ) {
    if ( p.kind == SGF_STAIR ) {
        // Open-riser treads rising 1/block toward aux; the matching floor
        // holes were carved in pass 2.
        int dx = 0, dz = 0;
        sgf_dir_delta( p.aux, &dx, &dz );
        const int sx = dx > 0 ? p.x0 : dx < 0 ? p.x1 : p.x0;
        const int sz = dz > 0 ? p.z0 : dz < 0 ? p.z1 : p.z0;
        for ( int i = 0; i < 8; i++ ) {
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
    // Deck-level pieces get support columns down to terrain (the upper keep
    // floor sits on the lower keep — its y0 > deck excludes it).
    if ( p.y0 != deck ) {
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

// Enumerate the fortress cells overlapped by this chunk's x/z box and run
// the full stamp for each that survives the existence + y-range gates.
void StructureGen::place_nether_fortress( Chunk &chunk, const int cox, const int coy, const int coz ) {
    const int cx0 = sgf_floor_div( cox - 1 );
    const int cx1 = sgf_floor_div( cox + CHUNK_SIZE_X );
    const int cz0 = sgf_floor_div( coz - 1 );
    const int cz1 = sgf_floor_div( coz + CHUNK_SIZE_Z );
    for ( int cx = cx0; cx <= cx1; cx++ ) {
        for ( int cz = cz0; cz <= cz1; cz++ ) {
            SGFAnchor a;
            if ( !sgf_for_cell( cx, cz, &a ) ) {
                continue;
            }
            const int deck = sgf_deck_y( a );
            if ( coy - 1 > deck + SGF_TOP_RISE || coy + CHUNK_SIZE_Y + 1 < deck - SGF_PILLAR_MAX ) {
                continue;
            }
            SGFLayout L;
            sgf_decode( a, L );
            for ( int i = 0; i < L.n_pieces; i++ ) {
                sgf_shell( chunk, L.pieces[ i ], cox, coy, coz );
            }
            for ( int i = 0; i < L.n_pieces; i++ ) {
                sgf_carve_piece( chunk, L, L.pieces[ i ], cox, coy, coz );
            }
            for ( int i = 0; i < L.n_pieces; i++ ) {
                sgf_detail( chunk, deck, L, L.pieces[ i ], cox, coy, coz );
            }
            // Chest + spawner slots.
            static const int chest_rot[ 4 ] = { 1, 3, 0, 2 }; // front: +x -x +z -z
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
    const int cx0 = sgf_floor_div( cox );
    const int cx1 = sgf_floor_div( cox + CHUNK_SIZE_X - 1 );
    const int cz0 = sgf_floor_div( coz );
    const int cz1 = sgf_floor_div( coz + CHUNK_SIZE_Z - 1 );
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
