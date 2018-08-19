#ifndef HEADER_BLOCK_H
#define HEADER_BLOCK_H

#include "constants.h"
#include "block_definitions.h"

#define FACE_TOP 0
#define FACE_BOTTOM 1
#define FACE_RIGHT 2
#define FACE_FRONT 3
#define FACE_LEFT 4
#define FACE_BACK 5

//  FR
// BR  FL
//  BL

#define CORNER_OFFSET_tfl 0
#define CORNER_OFFSET_tfr 2
#define CORNER_OFFSET_tbl 4
#define CORNER_OFFSET_tbr 6
#define CORNER_OFFSET_bfl 8
#define CORNER_OFFSET_bfr 10
#define CORNER_OFFSET_bbl 12
#define CORNER_OFFSET_bbr 14

#define CORNER_OFFSET_c 16

typedef struct {
    float x;
    float y;
    float z;

    float tex_coord_x; // Float just because it needs to be in shader, and we can avoid the gpu side cast
    float tex_coord_y;

    float which_face;
    float corner_shift;
} CubeFace;

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

#define WATER_HEIGHT ( 0.875f )

static CubeFace vd_data_water[] = {
    {1.0f * BLOCK_SCALE, WATER_HEIGHT, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_TOP}, // 18 0
    {0.0f * BLOCK_SCALE, WATER_HEIGHT, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_TOP}, // 19 1

    {1.0f * BLOCK_SCALE, WATER_HEIGHT, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_TOP}, // 22 2
    {0.0f * BLOCK_SCALE, WATER_HEIGHT, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_TOP}, // 23 3

    {0.5f * BLOCK_SCALE, WATER_HEIGHT, 0.5f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_TOP}, // 4
};
#define VB_DATA_SIZE_WATER ( 2 * 2 + 1 )

#endif