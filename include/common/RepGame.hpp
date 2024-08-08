#ifndef HEADER_REPGAME_H
#define HEADER_REPGAME_H

#include <stdio.h>

#include "common/Platforms.hpp"
#include "common/Logging.hpp"

struct RepGameState;

#include <entt/entity/registry.hpp>
#include <glm.hpp>
#include "common/RenderChain.hpp"
#include "common/RenderLink.hpp"

#include "chunk_loader.hpp"
#include "world.hpp"
#include "constants.hpp"
#include "ui_overlay.hpp"
#include "imgui_overlay.hpp"
#include "inventory.hpp"
#include "multiplayer.hpp"
#include "common/BlockUpdateQueue.hpp"

#include "common/utils/ecs.hpp"

#define GLM_FORCE_RADIANS
#include <gtc/type_ptr.hpp>
#include <gtc/matrix_transform.hpp>

#define DEBUG 1

#include "input.hpp"

struct RepGameState {
    double frame_rate;
    Multiplayer multiplayer;
    Input input;
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
    Inventory hotbar;
    struct {
        int selectionInBounds;
        int face;
        BlockID holdingBlock;
        glm::ivec3 pos_create;
        glm::ivec3 pos_destroy;
    } block_selection;
    BlockUpdateQueue blockUpdateQueue;
    long tick_number;
};

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

class RepGame {

    void add_to_an_inventory( BlockID blockId );
    void process_mouse_events( );
    void process_camera_angle( );
    void process_movement( );
    void process_block_updates( );
    void process_inventory_events( );

  public:
    void renderShaders( int x, int y, int z );
    RepGameState *init( const char *world_name, bool connect_multi, const char *host, bool supportsAnisotropicFiltering );
    void tick( );
    void clear( );
    void idle( );
    void draw( );
    void set_textures( unsigned int which_texture, unsigned char *textures, int textures_len );
    void cleanup( );
    Input &getInputState( );

    void changeSize( int x, int y );
    void get_screen_size( int *width, int *height );
    int shouldExit( );
    int should_lock_pointer( );
    static bool supportsAnisotropic( );

    int rep_tests_start( );
};

#endif