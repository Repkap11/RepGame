#pragma once

#include <stdio.h>

#include "common/Platforms.hpp"
#include "common/Logging.hpp"

struct RepGameState;

#include <entt/entity/registry.hpp>
#include <glm.hpp>
#include "common/RenderChain.hpp"
#include "common/RenderLink.hpp"

#include "common/utils/map_storage.hpp"
#include "chunk_loader.hpp"
#include "world.hpp"
#include "constants.hpp"
#include "ui_overlay.hpp"
#include "imgui_overlay.hpp"
#include "creative_inventory.hpp"
#include "hotbar.hpp"
#include "multiplayer.hpp"
#include "common/BlockUpdateQueue.hpp"

#include "common/utils/ecs.hpp"

#define GLM_FORCE_RADIANS
#include <gtc/type_ptr.hpp>
#include <gtc/matrix_transform.hpp>

#define DEBUG 1

#include "input.hpp"

struct RepGameState {
    MapStorage map_storage;
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
    CreativeInventory main_inventory;
    Hotbar hotbar;
    struct {
        int selectionInBounds;
        int face;
        glm::ivec3 pos_create;
        glm::ivec3 pos_destroy;
    } block_selection;
    BlockUpdateQueue blockUpdateQueue;
    long tick_number;
};

struct __attribute__( ( packed ) ) PlayerData {
    int reserved;
    float world_x;
    float world_y;
    float world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
};

class RepGame {
    RepGameState globalGameState;

    BlockID change_block( int place, BlockState blockState );
    unsigned char getPlacedRotation( BlockID blockID ) const;
    void initializeGameState( const char *world_name );
    void add_to_hotbar( bool alsoSelect, BlockID blockId );
    void process_mouse_events( );
    void process_camera_angle( );
    void process_movement( );
    void process_block_updates( );
    void process_inventory_events( );

  public:
    void renderShaders( int x, int y, int z );
    RepGameState *init( const char *world_name, bool connect_multi, const char *host, bool supportsAnisotropicFiltering );
    void tick( );
    static void clear( );
    void idle( );
    void draw( );
    static void set_textures( unsigned int which_texture, unsigned char *textures, int textures_len );
    void cleanup( );
    Input &getInputState( );

    void changeSize( int x, int y );
    void get_screen_size( int *width, int *height ) const;
    bool shouldExit( ) const;
    bool should_lock_pointer( ) const;
    static bool supportsAnisotropic( );

    int rep_tests_start( );
};