#ifndef HEADER_REPGAME_H
#define HEADER_REPGAME_H

#ifdef REPGAME_LINUX
#include "linux/RepGameLinux.h"
#else
//#include "andorid/RepGameAndroid.h"
#endif

#include "chunk_loader.h"
#include "constants.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define DEBUG 1

void showErrors( );

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
        TRIP_STATE( float m );
        float x;
        float y;
        float z;
        glm::mat4 view_look;
        glm::mat4 view_trans;
    } camera;
    struct {
        float width;
        float height;
        glm::mat4 proj;
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

void renderShaders( int x, int y, int z );
void repgame_init( );
void repgame_tick( );
void repgame_clear( );
void repgame_draw( );
void repgame_cleanup( );
void repgame_arrowKeyDownInput( int key, int x, int y );
void repgame_arrowKeyUpInput( int key, int x, int y );
void repgame_mouseInput( int button, int state, int x, int y );
void repgame_keysInput( unsigned char key, int x, int y, int pressed );
void repgame_mouseMove( int x, int y );
void repgame_changeSize( int x, int y );
void repgame_get_screen_size( int *width, int *height );
int repgame_shouldExit( );

#endif