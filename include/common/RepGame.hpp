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

#include <entt/entity/registry.hpp>
#include <glm.hpp>

#include "chunk_loader.hpp"
#include "world.hpp"
#include "constants.hpp"
#include "ui_overlay.hpp"
#include "imgui_overlay.hpp"
#include "inventory.hpp"
#include "common/BlockUpdateQueue.hpp"
#include "common/ecs_renderer.hpp"
#include "common/utils/ecs.hpp"

#define GLM_FORCE_RADIANS
#include <gtc/type_ptr.hpp>
#include <gtc/matrix_transform.hpp>

#define DEBUG 1

#include "input.hpp"

typedef struct {
    double frame_rate;
    InputState input;
    struct {
        float angle_H;
        float angle_V;
        glm::vec3 look;
        glm::mat4 rotation;
        glm::vec3 movement;
        glm::vec3 pos;
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
    ImGuiOverlay imgui_overlay;
    Inventory inventory;
    struct {
        int selectionInBounds;
        int face;
        BlockID holdingBlock;
        glm::ivec3 pos_create;
        glm::ivec3 pos_destroy;
    } block_selection;
    BlockUpdateQueue blockUpdateQueue;
    long tick_number;
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
    bool reflections_on;
} PlayerData;

void renderShaders( int x, int y, int z );
RepGameState *repgame_init( const char *world_name, bool connect_multi, const char *host, bool supportsAnisotropicFiltering );
void repgame_tick( );
void repgame_clear( );
void repgame_idle( );
void repgame_draw( );
void repgame_set_textures( unsigned int which_texture, unsigned char *textures, int textures_len );
void repgame_cleanup( );
InputState *repgame_getInputState( );

void repgame_changeSize( int x, int y );
void repgame_get_screen_size( int *width, int *height );
int repgame_shouldExit( );
int repgame_should_lock_pointer( );
bool repgame_supportsAnisotropic( );

int rep_tests_start( );

#endif