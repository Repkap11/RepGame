#ifndef HEADER_BLOCK_H
#define HEADER_BLOCK_H

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
    unsigned int tex_coord_x;
    unsigned int tex_coord_y;
    unsigned int which_face;
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

static CubeFace vd_data_solid[] = {
    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_FRONT}, // 0
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_FRONT}, // 1
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_FRONT}, // 2
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_FRONT}, // 3

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_BACK}, // 4
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_BACK}, // 5
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_BACK}, // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_BACK}, // 7

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_LEFT},  // 8
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT}, // 9
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT}, // 10
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_LEFT},  // 11

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_LEFT},  // 12
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT}, // 13
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT}, // 14
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_LEFT},  // 15

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, !0, FACE_BOTTOM}, // 16
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, !0, FACE_BOTTOM}, // 17
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, !1, FACE_TOP},    // 18
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, !1, FACE_TOP},    // 19

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM}, // 20
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM}, // 21
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP},    // 22
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP},    // 23
};
#define VB_DATA_SIZE_SOLID ( 4 * 6 )

#define WATER_HEIGHT ( 0.875f )

static CubeFace vd_data_water[] = {
    {1.0f, WATER_HEIGHT, 0.0f, /*Coords  Texture coords*/ !1, !1, FACE_TOP}, // 18 0
    {0.0f, WATER_HEIGHT, 0.0f, /*Coords  Texture coords*/ !0, !1, FACE_TOP}, // 19 1

    {1.0f, WATER_HEIGHT, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP}, // 22 2
    {0.0f, WATER_HEIGHT, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP}, // 23 3
};
#define VB_DATA_SIZE_WATER ( 2 * 2 )

#endif