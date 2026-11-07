#pragma once

// Shared tree builders, moved verbatim out of structure_gen.cpp so both the
// new terrain generator (Target = Chunk, writing chunk-local cells) and
// runtime sapling growth (Target = a world wrapper writing world cells) run
// the identical code. Always compiled — no REPGAME_MAP_GEN_LEGACY gate.
//
// Target provides:
//   BlockState get_block( const glm::ivec3 &pos ) const
//   void set_block( const glm::ivec3 &pos, const BlockState &state )
// Both are expected to bounds-check and no-op out-of-range cells, exactly
// like Chunk::get_block/set_block (out-of-range cells are no-ops — the chunk
// that owns them writes them on its own pass). `co` is the world origin of
// the local coords; world-space callers pass co = (0,0,0) so the hash inputs
// co + local stay identical.

#include <cstdlib>
#include <glm/glm.hpp>

#include "common/block_definitions.hpp"
#include "common/chunk_dimensions.hpp"

// ---------------------------------------------------------------------------
// Deterministic hashes. Structure gen only runs on the CPU, so these don't
// need GPU parity — but every chunk must evaluate the same rolls for shared
// (border-crossing) trees, so all rolls are pure functions of world coords.
// ---------------------------------------------------------------------------

static inline unsigned int sg_hash( const int x, const int y, const int z, const int seed ) {
    unsigned int h = 2166136261u;
    h = ( h ^ (unsigned int)x ) * 16777619u;
    h = ( h ^ (unsigned int)y ) * 16777619u;
    h = ( h ^ (unsigned int)z ) * 16777619u;
    h = ( h ^ (unsigned int)seed ) * 16777619u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

static inline float sg_roll( const int x, const int z, const int seed ) {
    return (float)( sg_hash( x, 0, z, seed ) & 0xFFFFFFu ) / 16777216.0f;
}

// ---------------------------------------------------------------------------
// Bounded writes. Local coords are in the chunk's array space: [-1, CHUNK_SIZE]
// per axis (the +/-1 border cells are included). Chunk::get_block/set_block
// already bounds-check, so out-of-range cells are no-ops — the chunk that owns
// them writes them on its own pass.
// ---------------------------------------------------------------------------

// Can a tree part overwrite this cell: air, liquids, and non-colliding plants.
template <class Target>
inline bool sg_can_replace( const Target &chunk, const int lx, const int ly, const int lz ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    if ( cur == AIR ) {
        return true;
    }
    if ( cur >= LAST_BLOCK_ID ) {
        return false;
    }
    return !block_definition_get_definition( cur )->collides_with_player;
}

template <class Target>
inline void sg_write_trunk( Target &chunk, const int lx, const int ly, const int lz, const BlockState &log_state, const BlockID leaf_id ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    if ( sg_can_replace( chunk, lx, ly, lz ) || cur == leaf_id ) {
        chunk.set_block( glm::ivec3( lx, ly, lz ), log_state );
    }
}

// Leaves only fill air: they never eat trunks, terrain, or a neighbor tree's
// canopy (contact seams read as natural gaps between crowns).
template <class Target>
inline void sg_write_leaf( Target &chunk, const int lx, const int ly, const int lz, const BlockState &leaf_state ) {
    if ( chunk.get_block( glm::ivec3( lx, ly, lz ) ).id == AIR ) {
        chunk.set_block( glm::ivec3( lx, ly, lz ), leaf_state );
    }
}

// Dirt under the trunk: rewrites the ground cell, but never eats a log from a
// neighboring tree's trunk.
template <class Target>
inline void sg_write_ground( Target &chunk, const int lx, const int ly, const int lz, const BlockState &dirt_state ) {
    const BlockID cur = chunk.get_block( glm::ivec3( lx, ly, lz ) ).id;
    switch ( cur ) {
        case OAK_LOG:
        case BIRCH_LOG:
        case JUNGLE_LOG:
        case ACACIA_LOG:
        case DARK_OAK_LOG:
        case CHERRY_LOG:
        case PALE_OAK_LOG:
        case MANGROVE_LOG:
        case MUSHROOM_STEM:
        case MANGROVE_ROOTS:
        case MUDDY_MANGROVE_ROOTS:
        case LAST_BLOCK_ID:
            return;
        default:
            chunk.set_block( glm::ivec3( lx, ly, lz ), dirt_state );
    }
}

// Unconditional canonical write — the caller checked replaceability.
template <class Target>
inline void sg_write_block( Target &chunk, const int lx, const int ly, const int lz, const BlockID id ) {
    const Block *def = block_definition_get_definition( id );
    chunk.set_block( glm::ivec3( lx, ly, lz ), { id, BLOCK_ROTATE_0, def->initial_redstone_power, id, 0 } );
}

// ---------------------------------------------------------------------------
// Trees
// ---------------------------------------------------------------------------

typedef enum {
    SHAPE_BLOB,     // oak / birch / dark oak / cherry / pale oak: ellipsoid crown
    SHAPE_PINE,     // spruce: shrinking stacked rings
    SHAPE_JUNGLE,   // tall trunk, wide flat crown
    SHAPE_ACACIA,   // leaning trunk, flat top
    SHAPE_MUSHROOM, // giant mushroom: stem + stepped cap discs
    SHAPE_MANGROVE, // stilt-rooted mangrove
} TreeShape;

typedef struct {
    BlockID log;
    BlockID leaf;
    TreeShape shape;
    int trunk_h;
    int canopy_r;
    int lean_x;
    int lean_z;
    int giant;        // 2x2 trunk
    BlockID hanger;   // AIR, or a hanging plant scattered under the canopy rim
    int hanger_len;   // max hanging strand length
    unsigned hanger_mask; // (sg_hash & mask) == 0 to hang — sparseness
} TreeSpec;

// Canopy edge-eating hashes take the chunk's world origin `co` and hash
// world cell coords: two chunks computing the same border-crossing tree must
// make the same call for the shared cell (halo mirroring depends on it).
template <class Target>
inline void sg_canopy_blob( Target &chunk, const int cx, const int cy, const int cz, const int rx, const int ry, const int rz, const BlockState &leaf, const int seed, const glm::ivec3 &co ) {
    const float fx = (float)( rx * rx ), fy = (float)( ry * ry ), fz = (float)( rz * rz );
    for ( int dy = -ry; dy <= ry; dy++ ) {
        for ( int dx = -rx; dx <= rx; dx++ ) {
            for ( int dz = -rz; dz <= rz; dz++ ) {
                const float d = (float)( dx * dx ) / fx + (float)( dy * dy ) / fy + (float)( dz * dz ) / fz;
                if ( d > 1.0f ) {
                    continue;
                }
                if ( d > 0.45f && ( sg_hash( co.x + cx + dx, co.y + cy + dy, co.z + cz + dz, seed ) & 7u ) < 3u ) {
                    continue;
                }
                sg_write_leaf( chunk, cx + dx, cy + dy, cz + dz, leaf );
            }
        }
    }
}

// Flat diamond-ish disc, with a sparser second layer above.
template <class Target>
inline void sg_canopy_disc( Target &chunk, const int cx, const int cy, const int cz, const int r, const BlockState &leaf, const int seed, const glm::ivec3 &co ) {
    for ( int dx = -r; dx <= r; dx++ ) {
        for ( int dz = -r; dz <= r; dz++ ) {
            const int d = abs( dx ) + abs( dz );
            if ( d > r + 1 ) {
                continue;
            }
            if ( d == r + 1 && ( sg_hash( co.x + cx + dx, co.y + cy, co.z + cz + dz, seed ) & 3u ) != 0u ) {
                continue; // eat most rim cells
            }
            sg_write_leaf( chunk, cx + dx, cy, cz + dz, leaf );
            if ( d < r - 1 && ( sg_hash( co.x + cx + dx, co.y + cy + 1, co.z + cz + dz, seed ) & 3u ) == 0u ) {
                sg_write_leaf( chunk, cx + dx, cy + 1, cz + dz, leaf );
            }
        }
    }
}

// Spruce: shrinking diamond rings every other layer up the trunk.
template <class Target>
inline void sg_canopy_pine( Target &chunk, const int bx, const int by, const int bz, const int trunk_h, const BlockState &leaf, const int seed, const glm::ivec3 &co ) {
    for ( int i = 2; i <= trunk_h; i += 2 ) {
        const int r = i < trunk_h - 3 ? 2 : 1;
        const int y = by + i;
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                const int d = abs( dx ) + abs( dz );
                if ( d > r + 1 ) {
                    continue;
                }
                if ( abs( dx ) == r && abs( dz ) == r && ( sg_hash( co.x + bx + dx, co.y + y, co.z + bz + dz, seed ) & 3u ) != 0u ) {
                    continue;
                }
                sg_write_leaf( chunk, bx + dx, y, bz + dz, leaf );
            }
        }
    }
    // Tip: a leaf right above the trunk top plus the point — odd trunk
    // heights would otherwise leave an air gap under the tip.
    sg_write_leaf( chunk, bx, by + trunk_h, bz, leaf );
    sg_write_leaf( chunk, bx, by + trunk_h + 1, bz, leaf );
}

// Hanging under-canopy scatter (pale moss, vines, propagules): a leaf cell
// with air below may grow a strand. Reads this chunk's own just-written
// canopy and hashes world coords, so border-crossing trees hang identically
// on both sides. One edge case: a leaf at world y == 1 (mod CHUNK_SIZE_Y)
// sits at local 63 for the chunk below — out of its readable band — so it
// can never evaluate that cell; both sides skip hangers there to stay
// mirror-consistent.
template <class Target>
inline void sg_canopy_hangers( Target &chunk, const TreeSpec &spec, const int bx, const int by, const int bz, const glm::ivec3 &co ) {
    const int r = spec.canopy_r + 1;
    const int y0 = by + spec.trunk_h - 3, y1 = by + spec.trunk_h + 3;
    for ( int ly = y0; ly <= y1; ly++ ) {
        for ( int dx = -r; dx <= r; dx++ ) {
            for ( int dz = -r; dz <= r; dz++ ) {
                if ( chunk.get_block( glm::ivec3( bx + dx, ly, bz + dz ) ).id != spec.leaf ) {
                    continue;
                }
                const int leaf_wy = co.y + ly;
                if ( ( leaf_wy % CHUNK_SIZE_Y + CHUNK_SIZE_Y ) % CHUNK_SIZE_Y == 1 ) {
                    continue;
                }
                if ( chunk.get_block( glm::ivec3( bx + dx, ly - 1, bz + dz ) ).id != AIR ) {
                    continue;
                }
                const unsigned int hh = sg_hash( co.x + bx + dx, co.y + ly, co.z + bz + dz, 20 );
                if ( ( hh & spec.hanger_mask ) != 0u ) {
                    continue;
                }
                const int len = 1 + (int)( ( hh >> 8 ) & 3u );
                for ( int i = 1; i <= len && i <= spec.hanger_len; i++ ) {
                    if ( chunk.get_block( glm::ivec3( bx + dx, ly - i, bz + dz ) ).id != AIR ) {
                        break;
                    }
                    sg_write_block( chunk, bx + dx, ly - i, bz + dz, spec.hanger );
                }
            }
        }
    }
}

// Bee nest on the trunk side of oak/birch — rare, one roll per tree cell.
template <class Target>
inline void sg_bee_nest( Target &chunk, const TreeSpec &spec, const int bx, const int by, const int bz, const glm::ivec3 &co ) {
    if ( spec.log != OAK_LOG && spec.log != BIRCH_LOG ) {
        return;
    }
    const int wx = co.x + bx;
    const int wy = co.y + by;
    const int wz = co.z + bz;
    if ( ( sg_hash( wx, wy, wz, 58 ) & 63u ) != 0u ) {
        return;
    }
    if ( chunk.get_block( glm::ivec3( bx + 1, by + 2, bz ) ).id == AIR ) {
        sg_write_block( chunk, bx + 1, by + 2, bz, BEE_NEST );
    }
}

template <class Target>
inline void sg_place_tree( Target &chunk, const TreeSpec &spec, const int bx, const int by, const int bz, const glm::ivec3 &co ) {
    const Block *log_def = block_definition_get_definition( spec.log );
    const Block *leaf_def = block_definition_get_definition( spec.leaf );
    const BlockState log_state = { spec.log, BLOCK_ROTATE_0, log_def->initial_redstone_power, spec.log, 0 };
    const BlockState leaf_state = { spec.leaf, BLOCK_ROTATE_0, leaf_def->initial_redstone_power, spec.leaf, 0 };
    const BlockState dirt_state = { DIRT, BLOCK_ROTATE_0, block_definition_get_definition( DIRT )->initial_redstone_power, DIRT, 0 };

    if ( spec.shape == SHAPE_MUSHROOM ) {
        // Giant mushrooms keep the mycelium under them.
    } else if ( spec.shape == SHAPE_MANGROVE ) {
        const Block *muddy_def = block_definition_get_definition( MUDDY_MANGROVE_ROOTS );
        const BlockState muddy_state = { MUDDY_MANGROVE_ROOTS, BLOCK_ROTATE_0, muddy_def->initial_redstone_power, MUDDY_MANGROVE_ROOTS, 0 };
        sg_write_ground( chunk, bx, by - 1, bz, muddy_state );
    }

    if ( spec.giant ) {
        // 2x2 trunk: ground + log for each of the four columns.
        sg_write_ground( chunk, bx, by - 1, bz, dirt_state );
        sg_write_ground( chunk, bx + 1, by - 1, bz, dirt_state );
        sg_write_ground( chunk, bx, by - 1, bz + 1, dirt_state );
        sg_write_ground( chunk, bx + 1, by - 1, bz + 1, dirt_state );
        for ( int i = 0; i < spec.trunk_h; i++ ) {
            sg_write_trunk( chunk, bx, by + i, bz, log_state, spec.leaf );
            sg_write_trunk( chunk, bx + 1, by + i, bz, log_state, spec.leaf );
            sg_write_trunk( chunk, bx, by + i, bz + 1, log_state, spec.leaf );
            sg_write_trunk( chunk, bx + 1, by + i, bz + 1, log_state, spec.leaf );
        }
    } else {
        if ( spec.shape != SHAPE_MUSHROOM && spec.shape != SHAPE_MANGROVE ) {
            sg_write_ground( chunk, bx, by - 1, bz, dirt_state );
        }
        for ( int i = 0; i < spec.trunk_h; i++ ) {
            const int lx = spec.lean_x != 0 && i >= spec.trunk_h - 2 ? bx + spec.lean_x : bx;
            const int lz = spec.lean_z != 0 && i >= spec.trunk_h - 2 ? bz + spec.lean_z : bz;
            sg_write_trunk( chunk, lx, by + i, lz, log_state, spec.leaf );
        }
    }
    const int tip_x = spec.lean_x != 0 ? bx + spec.lean_x : bx;
    const int tip_z = spec.lean_z != 0 ? bz + spec.lean_z : bz;
    const int top_y = by + spec.trunk_h;

    if ( spec.shape == SHAPE_MANGROVE ) {
        // Stilt roots arcing out from the trunk and down into the water/mud —
        // a sparse, uneven ring hashed on world coords.
        static const int ring[ 8 ][ 2 ] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
        for ( int o = 0; o < 8; o++ ) {
            const int rx = bx + ring[ o ][ 0 ], rz = bz + ring[ o ][ 1 ];
            if ( ( sg_hash( co.x + rx, co.y + by, co.z + rz, 19 ) & 3u ) == 0u ) {
                continue;
            }
            for ( int ry = by + 1; ry >= by - 6; ry-- ) {
                if ( !sg_can_replace( chunk, rx, ry, rz ) ) {
                    break;
                }
                sg_write_block( chunk, rx, ry, rz, MANGROVE_ROOTS );
            }
        }
    }

    switch ( spec.shape ) {
        case SHAPE_BLOB:
            sg_canopy_blob( chunk, bx, top_y - 1, bz, spec.canopy_r, 2, spec.canopy_r, leaf_state, 11, co );
            break;
        case SHAPE_PINE:
            sg_canopy_pine( chunk, bx, by, bz, spec.trunk_h, leaf_state, 12, co );
            break;
        case SHAPE_JUNGLE:
            sg_canopy_disc( chunk, bx, top_y, bz, spec.canopy_r, leaf_state, 13, co );
            sg_canopy_blob( chunk, bx, top_y + 1, bz, spec.canopy_r - 2, 1, spec.canopy_r - 2, leaf_state, 14, co );
            break;
        case SHAPE_ACACIA:
            sg_canopy_disc( chunk, tip_x, top_y, tip_z, spec.canopy_r, leaf_state, 15, co );
            break;
        case SHAPE_MUSHROOM:
            // Stepped cap: wide flat disc with a smaller one on top.
            sg_canopy_disc( chunk, bx, top_y - 1, bz, spec.canopy_r + 1, leaf_state, 16, co );
            sg_canopy_disc( chunk, bx, top_y, bz, spec.canopy_r, leaf_state, 17, co );
            break;
        case SHAPE_MANGROVE:
            sg_canopy_blob( chunk, bx, top_y - 1, bz, spec.canopy_r, 2, spec.canopy_r, leaf_state, 18, co );
            break;
    }

    if ( spec.hanger != AIR ) {
        sg_canopy_hangers( chunk, spec, bx, by, bz, co );
    }
    sg_bee_nest( chunk, spec, bx, by, bz, co );
}
