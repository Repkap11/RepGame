
#ifndef HEADER_OBJECT_H
#define HEADER_OBJECT_H
typedef struct {
    float x;
    float y;
    float z;
    float u;
    float v;
    unsigned int which_face;
} ObjectVertex;

static ObjectVertex vd_data_player_object[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_BACK}, // 0
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_BACK}, // 1
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_BACK}, // 2
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_BACK}, // 3

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_FRONT}, // 4
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_FRONT}, // 5
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_FRONT}, // 6
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_FRONT}, // 7

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_LEFT},  // 8
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT}, // 9
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT}, // 10
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_LEFT},  // 11

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_LEFT},  // 12
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT}, // 13
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT}, // 14
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_LEFT},  // 15

    {0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM}, // 16
    {1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM}, // 17
    {1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 0, 0, FACE_TOP},    // 18
    {0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ 1, 0, FACE_TOP},    // 19

    {0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM}, // 20
    {1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM}, // 21
    {1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 0, 1, FACE_TOP},    // 22
    {0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 1, 1, FACE_TOP},    // 23

    {0.5f, 1.0f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP},    // 24
    {0.5f, 0.0f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BOTTOM}, // 25
    {1.0f, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_RIGHT},  // 26
    {0.5f, 0.5f, 1.0f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_FRONT},  // 27
    {0.0f, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_LEFT},   // 28
    {0.5f, 0.5f, 0.0f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BACK},   // 29

};
#define VB_DATA_SIZE_PLAYER ( 5 * 6 )

typedef struct {
    unsigned short face[ NUM_FACES_IN_CUBE ];
    glm::mat4 transform;
} ObjectPosition;

#endif