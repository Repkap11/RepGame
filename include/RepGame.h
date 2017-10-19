#ifndef HEADER_TEST_H
#define HEADER_TEST_H

#include "chunk_loader.h"
#include "constants.h"
#include <stdio.h>

#define DEBUG 1

#define pr_debug( fmt, ... )                                                                                                                                                                                                                   \
    do {                                                                                                                                                                                                                                       \
        if ( DEBUG )                                                                                                                                                                                                                           \
            fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );                                                                                                                                            \
    } while ( 0 )

#define test pr_debug( );

#include "input.h"

typedef struct {
    double frame_rate;
    InputState input;
    struct {
        float angle_H;
        float angle_V;
        float lx;
        float ly;
        float lz;
        float x;
        float y;
        float z;
    } camera;
    struct {
        float width;
        float height;
    } screen;
    LoadedChunks gameChunks;

} RepGameState;

#endif