#pragma once

#include <stdio.h>

#include "common/Platforms.hpp"
#include "common/Logging.hpp"

struct RepGameState;

typedef enum {
    GameMode_Creative = 0,
    GameMode_Survival = 1,
} GameMode;

#include <entt/entity/registry.hpp>
#include <unordered_set>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm.hpp>
#include <gtx/hash.hpp>
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
#include "survival_inventory.hpp"
#include "font_renderer.hpp"
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
        // World-space position/velocity is double precision so movement and
        // collision stay correct at large coordinates (float32 would quantize
        // sub-block motion beyond ~2^24, and even walking speed at ~4M).
        glm::dvec3 pos;
        double y_speed;
        // Horizontal (x/z) velocity for momentum-based movement. Y is owned by
        // y_speed + gravity; this vector is only x and z.
        glm::dvec2 horizontal_vel;
        glm::mat4 view_look;
        glm::mat4 view_trans;
        int standing_on_solid;
        // Snapshot of the camera transform at the start of the most recent tick,
        // used to interpolate the rendered camera between the previous and current tick.
        glm::dvec3 prev_pos;
        float prev_angle_H;
        float prev_angle_V;
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
    FontRenderer font_renderer;
    CreativeInventory main_inventory;
    SurvivalInventory survival_inventory;
    Hotbar hotbar;
    GameMode game_mode;
    // Item stack currently being dragged with the mouse (Minecraft-style
    // click-to-pick-up / click-to-place). When is_holding_inventory_slot is
    // true, held_inventory_slot contains the stack picked up from a slot.
    InventorySlot held_inventory_slot;
    bool is_holding_inventory_slot;
    struct {
        int selectionInBounds;
        int face;
        glm::ivec3 pos_create;
        glm::ivec3 pos_destroy;
    } block_selection;
    // Survival-mode mining: while the left button is held on the same block,
    // progress_ticks accumulates once per tick until it reaches the block's
    // hardness * UPS_RATE, then the block breaks. Releasing the button,
    // aiming at a different block, or opening the inventory resets it.
    struct {
        glm::ivec3 pos;
        BlockID id;
        float progress_ticks;
    } block_mining;
    BlockUpdateQueue blockUpdateQueue;
    long tick_number;
    // Pressure plates with a live PressurePlateEvent check chain. Positions
    // are added when the player steps on a plate and removed by the event
    // when nobody is on it (or it was broken/unloaded), so exactly one check
    // chain exists per plate.
    std::unordered_set<glm::ivec3> watched_pressure_plates;
};

struct __attribute__( ( packed ) ) PlayerData {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    // Survival inventory (added after the original fields to preserve
    // backwards compatibility with older save files — old saves simply
    // won't have these bytes and the loader handles the shorter size).
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
};

class RepGame {
    RepGameState globalGameState;

    BlockID change_block( int place, BlockState blockState );
    void break_selected_block( );
    unsigned char getPlacedRotation( BlockID blockID ) const;
    void initializeGameState( const char *world_name );
    void add_to_hotbar( bool alsoSelect, BlockID blockId );
    void process_mouse_events( );
    void process_camera_angle( );
    // Builds the camera's look vector, rotation matrix, and view matrices from the
    // given angles and position. Shared by the simulation tick and the interpolated render path.
    void build_camera_view( float angle_H, float angle_V, const glm::dvec3 &pos, glm::vec3 &look, glm::mat4 &rotation, glm::mat4 &view_look, glm::mat4 &view_trans ) const;
    void process_movement( );
    void process_block_updates( );
    void process_inventory_events( );
    // Queues a PressurePlateEvent for each unpressed pressure plate the
    // player's feet overlap; pressed plates re-check themselves via the event.
    void process_pressure_plates( );

  public:
    // Per-frame profiling data (microseconds). Updated by draw() and read by
    // the platform frame loop to report a timing breakdown alongside FPS.
    struct FrameProfiling {
        long long us_tick;
        long long us_render;      // world.render (chunk loading/meshing on render thread)
        long long us_world_draw;  // world.draw (GL draw calls)
        long long us_ui_draw;     // ui_overlay + imgui
        long long us_total_draw;  // entire draw() call
        int num_drawable_chunks;
        int num_chunks_remeshed;
    } profiling;

    void renderShaders( int x, int y, int z );
    RepGameState *init( const char *world_name, bool connect_multi, const char *host, bool supportsAnisotropicFiltering );
    void tick( );
    static void clear( );
    void idle( );
    void draw( float alpha );
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