#ifndef HEADER_REPGAME_H
#define HEADER_REPGAME_H

#ifdef REPGAME_LINUX
#include "linux/RepGameLinux.hpp"
#endif
#ifdef REPGAME_ANDROID
#include "android/RepGameAndroid.hpp"
#endif
#ifdef REPGAME_WASM
#include "wasm/RepGameWASM.hpp"
#endif

#include "chunk_loader.hpp"
#include "constants.hpp"
#include "ui_overlay.hpp"

#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#define DEBUG 1

#define TRIP_STATE( replace )                                                                                                                                                                                                                  \
    replace##x;                                                                                                                                                                                                                                \
    replace##y;                                                                                                                                                                                                                                \
    replace##z
#define TRIP_ARGS( replace ) replace##x, replace##y, replace##z
#define TRIP_AND( replace ) ( replace##x && replace##y && replace##z )
#define TRIP_OR( replace ) ( replace##x || replace##y || replace##z )

#include "input.hpp"

typedef struct {
    double frame_rate;
    InputState input;
    struct {
        float angle_H;
        float angle_V;
        glm::vec3 look;
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
        glm::mat4 ortho;
        glm::mat4 ortho_center;
    } screen;
    LoadedChunks gameChunks;
    UIOverlay ui_overlay;
    struct {
        int selectionInBounds;
        int face;
        BlockID holdingBlock;
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
void repgame_set_textures( unsigned int which_texture, unsigned char *textures, int textures_len );
void repgame_cleanup( );
char *repgame_getShaderString( const char *fileName );
InputState *repgame_getInputState( );

void repgame_changeSize( int x, int y );
void repgame_get_screen_size( int *width, int *height );
int repgame_shouldExit( );

#endif