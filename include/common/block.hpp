#pragma once

#include <glm/glm.hpp>

#include "constants.hpp"
#include "block_definitions.hpp"

// See world.cpp for vbl
typedef struct {
    float x;
    float y;
    float z;

    float tex_coord_x; // Float just because it needs to be in shader, and we can avoid the gpu side cast
    float tex_coord_y;

    unsigned int which_face;
    unsigned int corner_shift;

} CubeFace;

// Order must match order of FACE_
// Two triangles per face: the per-vertex AO that needed a center vertex is
// gone, so faces are plain quads. Winding preserved from the old center fans.
static unsigned int ib_data_solid[] = {
    22, 18, 19, // Top
    22, 19, 23, //

    17, 21, 20, // Bottom
    17, 20, 16, //

    14, 13, 9,  // Right
    14, 9,  10, //

    7, 4, 5, // Front
    7, 5, 6, //

    11, 8,  12, // Left
    11, 12, 15, //

    2, 1, 0, // Back
    2, 0, 3, //
};
#define IB_SOLID_SIZE ( 3 * 2 * 6 )

static unsigned int ib_data_face[] = {
    2, 1, 4, //
    4, 1, 0, // Back
    0, 3, 4, //
    4, 3, 2, //
};
#define IB_FACE_SIZE ( 3 * 4 )

#define IB_POSITION_WATER_TOP 0
#define IB_POSITION_WATER_BOTTOM 1
static unsigned int ib_data_water[] = {
    2, 0, 1, // Top from the top
    2, 1, 3, //

    2, 3, 1, // Top, from the bottom
    2, 1, 0, //
};
#define IB_WATER_SIZE ( 3 * 2 * 2 )

#define WATER_HEIGHT ( 0.875f )

static CubeFace vd_data_water[] = {
    { 1.0f, WATER_HEIGHT, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_TOP, 0 }, // 18 0
    { 0.0f, WATER_HEIGHT, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_TOP, 0 }, // 19 1

    { 1.0f, WATER_HEIGHT, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_TOP, 0 }, // 22 2
    { 0.0f, WATER_HEIGHT, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_TOP, 0 }, // 23 3
};
#define VB_DATA_SIZE_WATER ( 2 * 2 )

static CubeFace vd_data_solid[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_BACK, CORNER_OFFSET_bbl }, // 0
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_BACK, CORNER_OFFSET_bbr }, // 1
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_BACK, CORNER_OFFSET_tbr }, // 2
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_BACK, CORNER_OFFSET_tbl }, // 3

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_FRONT, CORNER_OFFSET_bfl }, // 4
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_FRONT, CORNER_OFFSET_bfr }, // 5
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_FRONT, CORNER_OFFSET_tfr }, // 6
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_FRONT, CORNER_OFFSET_tfl }, // 7

    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_LEFT, CORNER_OFFSET_bbl },  // 8
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_RIGHT, CORNER_OFFSET_bbr }, // 9
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_RIGHT, CORNER_OFFSET_tbr }, // 10
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_LEFT, CORNER_OFFSET_tbl },  // 11

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_LEFT, CORNER_OFFSET_bfl },  // 12
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_RIGHT, CORNER_OFFSET_bfr }, // 13
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_RIGHT, CORNER_OFFSET_tfr }, // 14
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_LEFT, CORNER_OFFSET_tfl },  // 15

    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl }, // 16
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr }, // 17
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_TOP, CORNER_OFFSET_tbr },    // 18
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_TOP, CORNER_OFFSET_tbl },    // 19

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl }, // 20
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr }, // 21
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_TOP, CORNER_OFFSET_tfr },    // 22
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_TOP, CORNER_OFFSET_tfl },    // 23

    // Vertices for the two crossed "flower" quads (ib_data_flowers). The
    // quads' geometric crossing is always the centre of each diagonal, so
    // along their intersection both quads interpolate the identical texture
    // column (u=0.5, the texel 7|8 boundary for a 14-texel window). Where
    // that column is transparent both fragments discard at the same pixel
    // and the block behind peeks through the crossing. Shifting both quads'
    // tex_x window by half a texel moves the crossing sample onto the centre
    // of texel 7 — a texel-centre landing is required: it picks that texel
    // under NEAREST and gets no contribution from the neighbouring column
    // under LINEAR mipmapping. Flower textures keep the stem/blade at the
    // texture centre, so texel 7 is opaque at the crossing rows for both
    // quads, and the two quads' stems stay aligned at the seam.
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 0, FACE_BACK, CORNER_OFFSET_bbl },   // 24
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 0, FACE_RIGHT, CORNER_OFFSET_bfr },  // 25
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 1, FACE_RIGHT, CORNER_OFFSET_tfr },  // 26
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 1, FACE_BACK, CORNER_OFFSET_tbl },   // 27

    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 1, FACE_FRONT, CORNER_OFFSET_tfl },  // 28
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 0, FACE_RIGHT, CORNER_OFFSET_bbr },  // 29
    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 0, FACE_FRONT, CORNER_OFFSET_bfl },  // 30
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 1, FACE_RIGHT, CORNER_OFFSET_tbr },  // 31
};
#define VB_DATA_SIZE_SOLID ( 4 * 6 + 8 )

#define FACE_TOP 0
#define FACE_BOTTOM 1
#define FACE_RIGHT 2
#define FACE_FRONT 3
#define FACE_LEFT 4
#define FACE_BACK 5

static int FACE_DIR_X_OFFSETS[ NUM_FACES_IN_CUBE ] = { 0, 0, 1, 0, -1, 0 };
static int FACE_DIR_Y_OFFSETS[ NUM_FACES_IN_CUBE ] = { 1, -1, 0, 0, 0, 0 };
static int FACE_DIR_Z_OFFSETS[ NUM_FACES_IN_CUBE ] = { 0, 0, 0, 1, 0, -1 };
static int FACE_ROTATE_90[ NUM_FACES_IN_CUBE ] = { FACE_TOP, FACE_BOTTOM, FACE_BACK, FACE_RIGHT, FACE_FRONT, FACE_LEFT };

static int OPPOSITE_FACE[ NUM_FACES_IN_CUBE ] = { FACE_BOTTOM, FACE_TOP, FACE_LEFT, FACE_BACK, FACE_RIGHT, FACE_FRONT };

// Piston rotations encode the direction the head points: 0-3 are the usual yaw
// rotations (the shader face-shifts FACE_FRONT onto +z,-x,-z,+x respectively)
// and 4/5 are the vertical extensions.
#define PISTON_ROTATE_UP 4
#define PISTON_ROTATE_DOWN 5

// World face the head end of a piston points at for a given rotation.
static inline int piston_head_face( unsigned char rotation ) {
    static const int head_faces[ 6 ] = { FACE_FRONT, FACE_LEFT, FACE_BACK, FACE_RIGHT, FACE_TOP, FACE_BOTTOM };
    return head_faces[ rotation < 6 ? rotation : 0 ];
}

// Unit offset from a piston base toward where its head extends.
static inline glm::ivec3 piston_facing_dir( unsigned char rotation ) {
    const int face = piston_head_face( rotation );
    return glm::ivec3( FACE_DIR_X_OFFSETS[ face ], FACE_DIR_Y_OFFSETS[ face ], FACE_DIR_Z_OFFSETS[ face ] );
}

// Pixel-space (0-16) box of a piston head plate for each rotation. The mesh
// shader already yaws scale/offset for rotations 0-3, so block_adjust only
// needs this for 4/5; collision and ray code consume all six.
static inline void piston_head_shape( unsigned char rotation, short scale[ 3 ], short offset[ 3 ] ) {
    scale[ 0 ] = 16;
    scale[ 1 ] = 16;
    scale[ 2 ] = 16;
    offset[ 0 ] = 0;
    offset[ 1 ] = 0;
    offset[ 2 ] = 0;
    const int face = piston_head_face( rotation );
    // The plate is 4 pixels thick against the head-side face of the cell.
    if ( face == FACE_TOP ) {
        scale[ 1 ] = 4;
        offset[ 1 ] = 12;
    } else if ( face == FACE_BOTTOM ) {
        scale[ 1 ] = 4;
    } else if ( face == FACE_RIGHT ) {
        scale[ 0 ] = 4;
        offset[ 0 ] = 12;
    } else if ( face == FACE_LEFT ) {
        scale[ 0 ] = 4;
    } else if ( face == FACE_FRONT ) {
        scale[ 2 ] = 4;
        offset[ 2 ] = 12;
    } else { // FACE_BACK
        scale[ 2 ] = 4;
    }
}

// The cell-boundary face of an extended piston base that still fully covers
// its boundary: the one opposite the head end. Given as a def-space face index
// (the rotated_face/opposing_face space the meshers' coverage checks use):
// horizontal bases always cover their FACE_BACK slot, rot4 covers the world
// bottom (yaw table maps it to def BOTTOM), rot5 covers the world top.
static inline int piston_base_covered_face( unsigned char rotation ) {
    static const int covered[ 6 ] = { FACE_BACK, FACE_BACK, FACE_BACK, FACE_BACK, FACE_BOTTOM, FACE_TOP };
    return covered[ rotation < 6 ? rotation : 0 ];
}

// Pixel-space (0-16) box of an extended piston base: the full cube minus the
// 4-pixel slab at the head end, recessing the throat the arm slides through.
// Collision and ray code consume all six rotations; the mesh shader yaws
// scale/offset for rotations 0-3 so block_adjust only needs z for those.
static inline void piston_base_shape( unsigned char rotation, short scale[ 3 ], short offset[ 3 ] ) {
    scale[ 0 ] = 16;
    scale[ 1 ] = 16;
    scale[ 2 ] = 16;
    offset[ 0 ] = 0;
    offset[ 1 ] = 0;
    offset[ 2 ] = 0;
    const int face = piston_head_face( rotation );
    if ( face == FACE_TOP ) {
        scale[ 1 ] = 12;
    } else if ( face == FACE_BOTTOM ) {
        scale[ 1 ] = 12;
        offset[ 1 ] = 4;
    } else if ( face == FACE_RIGHT ) {
        scale[ 0 ] = 12;
    } else if ( face == FACE_LEFT ) {
        scale[ 0 ] = 12;
        offset[ 0 ] = 4;
    } else if ( face == FACE_FRONT ) {
        scale[ 2 ] = 12;
    } else { // FACE_BACK
        scale[ 2 ] = 12;
        offset[ 2 ] = 4;
    }
}

#define PIXEL_TO_FLOAT( pixel ) ( ( ( float )( pixel ) ) / ( 16.0f ) )

// See world.cpp for vbl
typedef struct {
    float x;
    float y;
    float z;

    unsigned char mesh_x;
    unsigned char mesh_y;
    unsigned char mesh_z;
    unsigned char space;

    unsigned short face[ NUM_FACES_IN_CUBE ];
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];
    unsigned int face_shift;

    // Pixel values (0-16 range, offset can be slightly negative for outsets).
    // Uploaded as signed bytes; the shader divides by 16.0 to get floats.
    signed char scale_x;
    signed char scale_y;
    signed char scale_z;

    signed char offset_x;
    signed char offset_y;
    signed char offset_z;

    signed char tex_offset_x;
    signed char tex_offset_y;
    signed char tex_offset_z;

    signed char pad[ 3 ]; // Align struct to 4 bytes (stride = 68)

} BlockCoords;

void block_adjust_coord_based_on_state( const Block *block, const BlockState *blockState, BlockCoords *blockCoord );