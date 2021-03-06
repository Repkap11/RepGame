#ifndef HEADER_REPGAME_H
#define HEADER_REPGAME_H

#include <stdio.h>

#ifdef REPGAME_LINUX
#include "linux/RepGameLinux.hpp"
#endif
#ifdef REPGAME_WINDOWS
#include "windows/RepGameWindows.hpp"
#endif
#ifdef REPGAME_ANDROID
#include "android/RepGameAndroid.hpp"
#endif
#ifdef REPGAME_WASM
#include "wasm/RepGameWASM.hpp"
#endif

#include "chunk_loader.hpp"
#include "world.hpp"
#include "constants.hpp"
#include "ui_overlay.hpp"
#include "inventory.hpp"
#include "common/BlockUpdateQueue.hpp"

#include <glm.hpp>
#define GLM_FORCE_RADIANS
#include <gtc/type_ptr.hpp>
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
        glm::mat4 rotation;
        TRIP_STATE( float m );
        float x;
        float y;
        float z;
        float y_speed;
        glm::mat4 view_look;
        glm::mat4 view_trans;
        int standing_on_solid;
    } camera;
    struct {
        float width;
        float height;
        glm::mat4 proj;
        glm::mat4 ortho;
        glm::mat4 ortho_center;
    } screen;
    Texture blocksTexture;
    World world;
    UIOverlay ui_overlay;
    Inventory inventory;
    struct {
        int selectionInBounds;
        int face;
        BlockID holdingBlock;
        TRIP_STATE( int create_ );
        TRIP_STATE( int destroy_ );
    } block_selection;
    BlockUpdateQueue blockUpdateQueue;
    long tick_number = 0;
} RepGameState;

typedef struct {
    BlockID holdingBlock;
    float world_x;
    float world_y;
    float world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
} PlayerData;

void renderShaders( int x, int y, int z );
void repgame_init( const char *world_name, bool connect_multi, const char *host );
void repgame_tick( );
void repgame_clear( );
void repgame_idle( );
void repgame_draw( );
void repgame_set_textures( unsigned int which_texture, unsigned char *textures, int textures_len );
void repgame_cleanup( );
char *repgame_getShaderString( const char *fileName );
InputState *repgame_getInputState( );

void repgame_changeSize( int x, int y );
void repgame_get_screen_size( int *width, int *height );
int repgame_shouldExit( );
int repgame_should_lock_pointer( );

int rep_tests_start( );

#endif