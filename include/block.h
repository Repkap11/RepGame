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

typedef struct {
    float x;
    float y;
    float z;

    float tex_coord_x;
    float tex_coord_y;

    unsigned int which_face;
    unsigned int corner_shift;
} CubeFace;

static unsigned int ib_data_solid[] = {
    22, 18, 19, // Top
    19, 23, 22, //

    17, 21, 20, // Bottom
    20, 16, 17, //

    2,  1,  0, // Right
    0,  3,  2, //

    14, 13, 9,  // Front
    9,  10, 14, //

    7,  4,  5, // Left
    5,  6,  7, //

    11, 8,  12, // Back
    12, 15, 11, //

};
#define IB_SOLID_SIZE ( 3 * 2 * 6 )

#define IB_POSITION_WATER_TOP 0
#define IB_POSITION_WATER_BOTTOM 1
static unsigned int ib_data_water[] = {
    2, 0, 1, // Top from the top
    1, 3, 2, //

    2, 3, 1, // Top, from the bottom
    1, 0, 2, //
};

#define CORNER_OFFSET_tfl 0
#define CORNER_OFFSET_tfr 2
#define CORNER_OFFSET_tbl 4
#define CORNER_OFFSET_tbr 6
#define CORNER_OFFSET_bfl 8
#define CORNER_OFFSET_bfr 10
#define CORNER_OFFSET_bbl 12
#define CORNER_OFFSET_bbr 14

static CubeFace vd_data_solid[] = {
    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_FRONT, CORNER_OFFSET_bfl}, // 0
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_FRONT, CORNER_OFFSET_bfr}, // 1
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_FRONT, CORNER_OFFSET_tfr}, // 2
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_FRONT, CORNER_OFFSET_tfl}, // 3

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_BACK, CORNER_OFFSET_bbl}, // 4
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_BACK, CORNER_OFFSET_bbr}, // 5
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_BACK, CORNER_OFFSET_tbr}, // 6
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_BACK, CORNER_OFFSET_tbl}, // 7

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_LEFT, CORNER_OFFSET_bfl},  // 8
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT, CORNER_OFFSET_bfr}, // 9
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT, CORNER_OFFSET_tfr}, // 10
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_LEFT, CORNER_OFFSET_tfl},  // 11

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_LEFT, CORNER_OFFSET_bbl},  // 12
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT, CORNER_OFFSET_bbr}, // 13
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT, CORNER_OFFSET_tbr}, // 14
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_LEFT, CORNER_OFFSET_tbl},  // 15

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM, CORNER_OFFSET_bfl}, // 16
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM, CORNER_OFFSET_bfr}, // 17
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_TOP, CORNER_OFFSET_tfr},    // 18
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_TOP, CORNER_OFFSET_tfl},    // 19

    {0.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM, CORNER_OFFSET_bbl}, // 20
    {1.0f * BLOCK_SCALE, 0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM, CORNER_OFFSET_bbr}, // 21
    {1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_TOP, CORNER_OFFSET_tbr},    // 22
    {0.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_TOP, CORNER_OFFSET_tbl},    // 23
};
#define VB_DATA_SIZE_SOLID ( 4 * 6 )

#define WATER_HEIGHT ( 0.875f )

static CubeFace vd_data_water[] = {
    {1.0f * BLOCK_SCALE, WATER_HEIGHT, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 0, FACE_TOP}, // 18 0
    {0.0f * BLOCK_SCALE, WATER_HEIGHT, 0.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 0, FACE_TOP}, // 19 1

    {1.0f * BLOCK_SCALE, WATER_HEIGHT, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 0, 1, FACE_TOP}, // 22 2
    {0.0f * BLOCK_SCALE, WATER_HEIGHT, 1.0f * BLOCK_SCALE, /*Coords  Texture coords*/ 1, 1, FACE_TOP}, // 23 3
};
#define VB_DATA_SIZE_WATER ( 2 * 2 )

#endif