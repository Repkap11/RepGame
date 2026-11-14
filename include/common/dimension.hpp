#pragma once

// ---------------------------------------------------------------------------
// dimension.hpp — the 4 dimension slots encoded in chunk-Y.
//
// The world has no separate per-dimension coordinate spaces: a dimension is a
// 64-chunk band of the signed-8-bit chunk-y field (the narrowest field of the
// packed natural-mob id, see net/packet.hpp). Two high bits = dimension slot,
// six low bits = chunk y within the slot:
//
//   encoded chunk y in [-32, 31]              -> DIM_OVERWORLD (0)
//   encoded chunk y in [-96,-33]              -> DIM_NETHER    (1)
//   encoded chunk y in [ 32, 95]              -> DIM_SPARE_2   (2, reserved)
//   encoded chunk y in [ 96,127] + [-128,-97] -> DIM_SPARE_3   (3, wraps)
//
// Each band is 64 chunks = 3968 blocks of local height. The overworld band is
// centered on y=0 so existing saves stay in dimension 0. Above it sit ~30
// chunks of overworld bedrock floor (blocks -1984..-101 are bedrock under the
// legacy/new generators), which doubles as the nether ceiling; the margin is
// far wider than CHUNK_RADIUS_Y, so a loaded torus never spans two bands —
// except at a band edge where the neighbor band is solid filler anyway.
//
// The #defines above the REPGAME_WGSL_GEN guard are safe to expand inside the
// mapgen dialect (map_logic.hpp): pure integer comparisons only, no ternary.
// The C++ API below is host-only.
// ---------------------------------------------------------------------------

#define DIM_ID_OVERWORLD 0
#define DIM_ID_NETHER 1
#define DIM_ID_SPARE_2 2
#define DIM_ID_SPARE_3 3
#define DIM_COUNT 4

// Chunks per dimension band (2^(8 - DIM_BITS) of the signed-8-bit field).
#define DIM_CHUNK_SPAN 64
// Blocks per band: 64 * CHUNK_SIZE_Y(62). Kept a literal so this header never
// needs chunk_dimensions.hpp inside the WGSL pass; the static_assert in
// map_gen.cpp ties them together.
#define DIM_BLOCK_SPAN 3968

// Encoded chunk-y bounds of each band.
#define DIM_CY_MIN_O ( -32 )
#define DIM_CY_MAX_O 31
#define DIM_CY_MIN_N ( -96 )
#define DIM_CY_MAX_N ( -33 )
#define DIM_CY_MIN_2 32
#define DIM_CY_MAX_2 95
// Band 3 wraps the signed-8-bit edge: [96,127] then [-128,-97].
#define DIM_CY_MIN_3_HI 96
#define DIM_CY_MAX_3_LO ( -97 )

// Encoded block-y bounds (chunk bound * 62, band edge included).
#define DIM_BY_MIN_O ( -1984 )
#define DIM_BY_MAX_O 1983
#define DIM_BY_MIN_N ( -5952 )
#define DIM_BY_MAX_N ( -1985 )
#define DIM_BY_MIN_2 1984
#define DIM_BY_MAX_2 5951
#define DIM_BY_MIN_3_HI 5952
#define DIM_BY_MAX_3_HI 7935
#define DIM_BY_MIN_3_LO ( -7936 )
#define DIM_BY_MAX_3_LO ( -5953 )

// WGSL-safe band predicates on absolute (encoded) block y.
#define DIM_IS_OVERWORLD_BY( by ) ( ( by ) >= DIM_BY_MIN_O && ( by ) <= DIM_BY_MAX_O )
#define DIM_IS_NETHER_BY( by ) ( ( by ) >= DIM_BY_MIN_N && ( by ) <= DIM_BY_MAX_N )

#if !defined( REPGAME_WGSL_GEN )

#include <string.h>
#include <glm/glm.hpp>

enum DimensionId : int {
    DIM_OVERWORLD = DIM_ID_OVERWORLD,
    DIM_NETHER = DIM_ID_NETHER,
    DIM_SPARE_2 = DIM_ID_SPARE_2,
    DIM_SPARE_3 = DIM_ID_SPARE_3,
};

// Fold a raw chunk y into the signed-8-bit field the mob id stores.
static inline int dim_wrap_chunk_y( const int cy ) {
    return ( ( cy + 128 ) & 255 ) - 128;
}

static inline DimensionId dimension_of_chunk( const int cy_ ) {
    const int cy = dim_wrap_chunk_y( cy_ );
    if ( cy >= DIM_CY_MIN_O && cy <= DIM_CY_MAX_O ) {
        return DIM_OVERWORLD;
    }
    if ( cy >= DIM_CY_MIN_N && cy <= DIM_CY_MAX_N ) {
        return DIM_NETHER;
    }
    if ( cy >= DIM_CY_MIN_2 && cy <= DIM_CY_MAX_2 ) {
        return DIM_SPARE_2;
    }
    return DIM_SPARE_3;
}

static inline DimensionId dimension_of_chunk( const glm::ivec3 &chunk_pos ) {
    return dimension_of_chunk( chunk_pos.y );
}

static inline DimensionId dimension_of_block_y( const int by ) {
    if ( DIM_IS_OVERWORLD_BY( by ) ) {
        return DIM_OVERWORLD;
    }
    if ( DIM_IS_NETHER_BY( by ) ) {
        return DIM_NETHER;
    }
    if ( by >= DIM_BY_MIN_2 && by <= DIM_BY_MAX_2 ) {
        return DIM_SPARE_2;
    }
    return DIM_SPARE_3;
}

static inline DimensionId dimension_of_pos( const glm::dvec3 &pos ) {
    return dimension_of_block_y( static_cast<int>( glm::floor( pos.y ) ) );
}
static inline DimensionId dimension_of_pos( const glm::vec3 &pos ) {
    return dimension_of_block_y( static_cast<int>( glm::floor( pos.y ) ) );
}
static inline DimensionId dimension_of_pos( const glm::ivec3 &pos ) {
    return dimension_of_block_y( pos.y );
}

// Local y within the band: [0, DIM_CHUNK_SPAN) for chunks, [0, DIM_BLOCK_SPAN)
// for blocks. Inputs outside the band's encoded range are clamped by
// dimension_of_* into DIM_SPARE_3, whose local axis runs [96,127] first then
// the wrapped [-128,-97] half.
static inline int dim_local_chunk_y( const int cy_ ) {
    const int cy = dim_wrap_chunk_y( cy_ );
    switch ( dimension_of_chunk( cy ) ) {
        case DIM_OVERWORLD:
            return cy - DIM_CY_MIN_O;
        case DIM_NETHER:
            return cy - DIM_CY_MIN_N;
        case DIM_SPARE_2:
            return cy - DIM_CY_MIN_2;
        default:
            return cy >= DIM_CY_MIN_3_HI ? cy - DIM_CY_MIN_3_HI : cy + 128 + 32;
    }
}

static inline int dim_local_block_y( const int by ) {
    switch ( dimension_of_block_y( by ) ) {
        case DIM_OVERWORLD:
            return by - DIM_BY_MIN_O;
        case DIM_NETHER:
            return by - DIM_BY_MIN_N;
        case DIM_SPARE_2:
            return by - DIM_BY_MIN_2;
        default:
            return by >= DIM_BY_MIN_3_HI ? by - DIM_BY_MIN_3_HI : by - DIM_BY_MIN_3_LO + 1984;
    }
}

// Inverse of the locals above: (dimension, local) -> encoded coordinate.
static inline int dim_world_chunk_y( const DimensionId dim, const int local_cy ) {
    switch ( dim ) {
        case DIM_OVERWORLD:
            return local_cy + DIM_CY_MIN_O;
        case DIM_NETHER:
            return local_cy + DIM_CY_MIN_N;
        case DIM_SPARE_2:
            return local_cy + DIM_CY_MIN_2;
        default:
            return local_cy < 32 ? local_cy + DIM_CY_MIN_3_HI : local_cy - 128 - 32;
    }
}

static inline int dim_world_block_y( const DimensionId dim, const int local_by ) {
    switch ( dim ) {
        case DIM_OVERWORLD:
            return local_by + DIM_BY_MIN_O;
        case DIM_NETHER:
            return local_by + DIM_BY_MIN_N;
        case DIM_SPARE_2:
            return local_by + DIM_BY_MIN_2;
        default:
            return local_by < 1984 ? local_by + DIM_BY_MIN_3_HI : local_by - 1984 + DIM_BY_MIN_3_LO;
    }
}

static inline const char *dim_name( const DimensionId dim ) {
    switch ( dim ) {
        case DIM_OVERWORLD:
            return "overworld";
        case DIM_NETHER:
            return "nether";
        case DIM_SPARE_2:
            return "dim2";
        default:
            return "dim3";
    }
}

static inline DimensionId dim_from_name( const char *name ) {
    if ( name == nullptr ) {
        return DIM_OVERWORLD;
    }
    if ( name[ 0 ] == 'n' || !strcmp( name, "1" ) ) {
        return DIM_NETHER;
    }
    if ( !strcmp( name, "2" ) ) {
        return DIM_SPARE_2;
    }
    if ( !strcmp( name, "3" ) ) {
        return DIM_SPARE_3;
    }
    return DIM_OVERWORLD;
}

#endif // !REPGAME_WGSL_GEN
