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

#define TRIP_STATE( replace ) replace##x; replace##y; replace##z
#define TRIP_ARGS( replace ) replace##x, replace##y, replace##z
#define TRIP_AND( replace ) ( replace##x && replace##y && replace##z )

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
    struct {
        int show;
        BlockID blockID;
        int create_x;
        int create_y;
        int create_z;
        int destroy_x;
        int destroy_y;
        int destroy_z;
    } block_selection;

} RepGameState;

#endif