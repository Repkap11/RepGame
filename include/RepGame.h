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

#define TRIP_STATE( replace )                                                                                                                                                                                                                  \
    replace##x;                                                                                                                                                                                                                                \
    replace##y;                                                                                                                                                                                                                                \
    replace##z
#define TRIP_ARGS( replace ) replace##x, replace##y, replace##z
#define TRIP_AND( replace ) ( replace##x && replace##y && replace##z )
#define TRIP_OR( replace ) ( replace##x || replace##y || replace##z )

#include "input.h"

typedef struct {
    double frame_rate;
    InputState input;
    struct {
        float angle_H;
        float angle_V;
        TRIP_STATE( float l );
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
        TRIP_STATE( int create_ );
        TRIP_STATE( int destroy_ );
    } block_selection;
    struct {
        Chunk *chunk;
        TRIP_STATE( float diff_ );
    } physics;

} RepGameState;

#endif