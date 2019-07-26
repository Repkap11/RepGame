#ifndef HEADER_BLOCK_H
#define HEADER_BLOCK_H

#include "constants.hpp"
#include "block_definitions.hpp"

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
static unsigned int ib_data_solid[] = {
    22, 18, 24, //
    24, 18, 19, // Top
    19, 23, 24, //
    24, 23, 22, //

    17, 21, 25, // Bottom
    25, 21, 20, //
    20, 16, 25, //
    25, 16, 17, //

    14, 13, 26, //
    26, 13, 9,  // Right
    9,  10, 26, //
    26, 10, 14, //

    7,  4,  27, //
    27, 4,  5,  // Front
    5,  6,  27, //
    27, 6,  7,  //

    11, 8,  28, //
    28, 8,  12, // Left
    12, 15, 28, //
    28, 15, 11, //

    2,  1,  29, //
    29, 1,  0,  // Back
    0,  3,  29, //
    29, 3,  2,  //

};
#define IB_SOLID_SIZE ( 3 * 4 * 6 )

#define IB_POSITION_WATER_TOP 0
#define IB_POSITION_WATER_BOTTOM 1
static unsigned int ib_data_water[] = {
    2, 0, 4, //
    4, 0, 1, // Top from the top
    1, 3, 4, //
    4, 3, 2, //

    2, 3, 4, //
    4, 3, 1, // Top, from the bottom
    1, 0, 4, //
    4, 0, 2, //
};
#define IB_WATER_SIZE ( 3 * 4 * 2 )

#define WATER_HEIGHT ( 0.875f )

static CubeFace vd_data_water[] = {
    {1.0f, WATER_HEIGHT, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_TOP, 0}, // 18 0
    {0.0f, WATER_HEIGHT, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_TOP, 0}, // 19 1

    {1.0f, WATER_HEIGHT, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP, 0}, // 22 2
    {0.0f, WATER_HEIGHT, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP, 0}, // 23 3

    {0.5f, WATER_HEIGHT, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP, 0}, // 4
};
#define VB_DATA_SIZE_WATER ( 2 * 2 + 1 )

static CubeFace vd_data_solid[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_BACK, CORNER_OFFSET_bbl}, // 0
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_BACK, CORNER_OFFSET_bbr}, // 1
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_BACK, CORNER_OFFSET_tbr}, // 2
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_BACK, CORNER_OFFSET_tbl}, // 3

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_FRONT, CORNER_OFFSET_bfl}, // 4
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_FRONT, CORNER_OFFSET_bfr}, // 5
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_FRONT, CORNER_OFFSET_tfr}, // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_FRONT, CORNER_OFFSET_tfl}, // 7

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_LEFT, CORNER_OFFSET_bbl},  // 8
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT, CORNER_OFFSET_bbr}, // 9
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT, CORNER_OFFSET_tbr}, // 10
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_LEFT, CORNER_OFFSET_tbl},  // 11

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_LEFT, CORNER_OFFSET_bfl},  // 12
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT, CORNER_OFFSET_bfr}, // 13
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT, CORNER_OFFSET_tfr}, // 14
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_LEFT, CORNER_OFFSET_tfl},  // 15

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl}, // 16
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr}, // 17
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_TOP, CORNER_OFFSET_tbr},    // 18
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_TOP, CORNER_OFFSET_tbl},    // 19

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl}, // 20
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr}, // 21
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP, CORNER_OFFSET_tfr},    // 22
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP, CORNER_OFFSET_tfl},    // 23

    {0.5f, 1.0f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP, CORNER_OFFSET_c},    // 24
    {0.5f, 0.0f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BOTTOM, CORNER_OFFSET_c}, // 25
    {1.0f, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_RIGHT, CORNER_OFFSET_c},  // 26
    {0.5f, 0.5f, 1.0f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_FRONT, CORNER_OFFSET_c},  // 27
    {0.0f, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_LEFT, CORNER_OFFSET_c},   // 28
    {0.5f, 0.5f, 0.0f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BACK, CORNER_OFFSET_c},   // 29
};
#define VB_DATA_SIZE_SOLID ( 5 * 6 )

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

} BlockCoords;

#endif