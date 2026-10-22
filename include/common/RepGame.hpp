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
#include <gtc/quaternion.hpp>

#define DEBUG 1

#include "input.hpp"

// One render target view (a VR eye). Flat rendering passes views==nullptr to
// draw_frame, which uses screen.proj and the interpolated camera instead.
// Positions are in tracking (LOCAL) space; meters == blocks.
struct RenderView {
    glm::mat4 proj;        // per-eye projection
    glm::quat orientation; // eye orientation in tracking space
    glm::vec3 position;    // eye position in tracking space
    unsigned int fbo;      // target framebuffer
    int width, height;
};

// Held state of the VR controls for this frame (the platform fills it after
// xrSyncActions); RepGame does all edge detection per tick.
struct VrControllerInput {
    glm::vec2 move = glm::vec2( 0.0f ); // left thumbstick, x right / y forward
    glm::vec2 turn = glm::vec2( 0.0f ); // right thumbstick
    bool jump = false, sneak = false, sprint = false;
    bool break_block = false, place_block = false, pick_block = false;
    bool hotbar_next = false, hotbar_prev = false, inventory = false, toggle_fly = false;
    bool aim_valid = false;
    glm::quat aim_orientation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f ); // right aim pose, tracking (LOCAL) space
    glm::vec3 aim_position = glm::vec3( 0.0f );
};

// Placement of the VR UI quad, decided by the game, submitted by the platform.
struct VrUiLayer {
    bool world_locked = false; // true: pose is in tracking (LOCAL) space; false: head-locked (VIEW space)
    glm::quat orientation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
    glm::vec3 position = glm::vec3( 0.0f ); // quad center, meters
    glm::vec2 size = glm::vec2( 0.0f );     // meters (width, height)
};

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
        // Sneak pose: while sneaking the collision box is shorter and the eye
        // sits lower. eye_height is the feet-to-eye distance used to derive the
        // AABB (feet = pos.y - eye_height). sneaking may stay set after the key
        // is released if there's no headroom to stand.
        int sneaking = 0;
        double player_height = PLAYER_HEIGHT;
        double eye_height = PLAYER_EYE_HEIGHT;
        // Render-only smoothed eye height so the camera glides down/up over a
        // few ticks on pose changes instead of snapping.
        double eye_height_render = PLAYER_EYE_HEIGHT;
        // Snapshot of the camera transform at the start of the most recent tick,
        // used to interpolate the rendered camera between the previous and current tick.
        glm::dvec3 prev_pos;
        double prev_eye_height = PLAYER_EYE_HEIGHT;
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
    // Chest container the player currently has open (at most one). Contents
    // live in the owning chunk (World::get_chest_contents); this renderer
    // draws them as a top-aligned panel.
    struct {
        bool open;
        glm::ivec3 pos;
        InventoryRenderer inventory_renderer;
    } chest;
    // Chests this player broke while connected to a server: the server answers
    // the block removal with a CHEST_CONTENTS snapshot used for looting, so
    // contents are never lost to a client that never opened the chest.
    std::unordered_set<glm::ivec3> pending_chest_loot;
    struct {
        int selectionInBounds;
        int face;
        glm::ivec3 pos_create;
        glm::ivec3 pos_destroy;
        // Second raycast that can hit water — only run while holding a
        // can_float_on_water block, and only used to place it.
        int floatSelectionInBounds;
        glm::ivec3 pos_create_water;
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
    // Fresh spawns (no player.dat) start on the terrain-height estimate but
    // can't see structure blocks until the spawn column is generated. While
    // true, RepGame::tick waits for the column to load and snaps the player
    // onto the topmost colliding block.
    bool spawn_pending;
    long tick_number;
    // Day/night clock in ticks (see DAY_LENGTH_TICKS in light.hpp).
    // Persisted in player.dat via PlayerData::world_time.
    long world_time;
    // Pressure plates with a live PressurePlateEvent check chain. Positions
    // are added when the player steps on a plate and removed by the event
    // when nobody is on it (or it was broken/unloaded), so exactly one check
    // chain exists per plate.
    std::unordered_set<glm::ivec3> watched_pressure_plates;
    // VR state (OpenXR session). active means stereo rendering is in use:
    // mouse dx then turns the body (body_yaw) and the HMD's local orientation
    // supplies head — camera.angle_H/V are recomputed as body+head each tick
    // so raycasts, movement and remote avatars track the real head.
    struct {
        bool active = false;
        float body_yaw = 0.0f;
        glm::quat head = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
        glm::vec3 head_pos = glm::vec3( 0.0f ); // head position, tracking (LOCAL) space
        // UI quad state: pinned in LOCAL space while the inventory is open
        // (anchored on the false→true edge), head-locked otherwise.
        bool ui_world_locked = false;
        glm::quat ui_orientation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
        glm::vec3 ui_position = glm::vec3( 0.0f );
        bool prev_inventory_open = false;
        // Set by the pointer raycast when the aim ray lands on the UI quad —
        // gates the pointer marker drawn into the UI layer.
        bool pointer_on_ui = false;
        // Flat-path draw quality saved when entering VR (VR defaults to LOW
        // via REPGAME_VR_QUALITY); player.dat always stores this, never the
        // live VR quality.
        WorldDrawQuality saved_quality = WorldDrawQuality::MEDIUM;
        // Controller state: input is this frame's synced actions, prev_input
        // the previous tick's (process_vr_input does all edge detection).
        VrControllerInput input, prev_input;
        // Thumbstick bookkeeping: move_active tracks "stick is driving
        // movement.sizeH" so an idle stick hands control back to the
        // keyboard; turn_armed is the snap-turn re-arm flag.
        bool move_active = false;
        bool turn_armed = true;
        // Set when the player breaks a block; the platform turns it into a
        // controller haptic pulse via consume_vr_haptic().
        bool haptic_pending = false;
    } vr;
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
    // Day/night clock; appended last so old player.dat files keep loading
    // (map_storage accepts the pre-world_time layout separately).
    long world_time;
    // Player's horizontal render distance in chunks; appended last like
    // world_time (map_storage accepts the pre-render_distance layout).
    // 0 in old saves → absent, the default is used instead.
    int render_distance;
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
    // Chest open/close (sends CHEST_OPEN/CHEST_CLOSE when connected).
    void open_chest( const glm::ivec3 &pos );
    void close_chest( );
    // Shared body of draw()/draw_vr(): everything once per frame, then the
    // world-draw section once per view (flat: a single pass with the screen
    // projection and interpolated camera).
    void draw_frame( float alpha, const RenderView *views, int num_views );
    // Applies vr.input (edge-detected against vr.prev_input) onto the shared
    // Input fields — movement stick, jump/sneak/sprint, hotbar steps, mouse
    // buttons, snap/smooth turn. Called at the top of tick() in VR.
    void process_vr_input( );
    // The UI draw sequence shared by the flat tail and draw_vr_ui: atlas
    // bind + CLAMP wrap, depth/stencil clear, ui_overlay + held item +
    // VR pointer marker. Caller owns the FBO/bind/blend state.
    void draw_ui_pass( const glm::dvec3 &render_pos, float daylight );

  public:
    // Per-frame profiling data (microseconds). Updated by draw() and read by
    // the platform frame loop to report a timing breakdown alongside FPS.
    struct FrameProfiling {
        long long us_tick;
        long long us_render;      // world.render (chunk loading/meshing on render thread)
        long long us_world_draw;  // world.draw (GL draw calls)
        long long us_ui_draw;     // ui_overlay + imgui
        long long us_total_draw;  // entire draw() call
        long long us_swap;        // SDL_GL_SwapWindow (GPU queue backpressure)
        int num_drawable_chunks;
        int num_chunks_remeshed;
    } profiling;

    void renderShaders( int x, int y, int z );
    RepGameState *init( const char *world_name, bool connect_multi, const char *host, bool supportsAnisotropicFiltering );
    void tick( );
    static void clear( );
    void idle( );
    void draw( float alpha );
    // Stereo draw: one pass per RenderView into its swapchain FBO.
    void draw_vr( float alpha, const RenderView *views, int num_views );
    // VR session lifecycle glue (called by the platform loop).
    void set_vr_active( bool active );
    void set_vr_head( const glm::quat &head, const glm::vec3 &head_pos );
    // Per-frame controller state from the platform's action sync.
    void set_vr_input( const VrControllerInput &in );
    // Returns (and clears) the pending block-break haptic flag.
    bool consume_vr_haptic( );
    // The UI quad placement for the platform's composition layer this frame.
    VrUiLayer get_vr_ui_layer( ) const;
    // Render the UI overlay into the platform's UI swapchain image FBO
    // (premultiplied alpha out for the compositor).
    void draw_vr_ui( unsigned int fbo, int width, int height );
    // Off-axis perspective projection from per-edge FOV tangents (radians,
    // OpenXR convention: angleLeft/angleDown are negative). Matches
    // glm::perspective for symmetric FOVs; GL [-1,1] clip depth.
    static glm::mat4 projection_from_fov( float angle_left, float angle_right, float angle_up, float angle_down, float near_z, float far_z );
    static void set_textures( unsigned int which_texture, unsigned char *textures, int textures_len );
    void cleanup( );
    // Writes current position/inventory/mode/clock to player.dat. Called by
    // cleanup() and, on WASM, periodically so crashes don't lose player state.
    void save_player_data( );
    Input &getInputState( );
    GameMode getGameMode( ) const;

    void changeSize( int x, int y );
    void get_screen_size( int *width, int *height ) const;
    bool shouldExit( ) const;
    bool should_lock_pointer( ) const;
    static bool supportsAnisotropic( );
    // True when REPGAME_CAPTURE is set (deterministic screenshot capture mode).
    static bool capture_mode( );
    // True when REPGAME_FLY is set (scripted no-clip flight + FLYMET metrics).
    static bool fly_mode( );

    int rep_tests_start( );
};

// Moves every non-empty slot of a chest into the player's inventories
// (survival first, hotbar for the overflow). Used for chest-break looting;
// the multiplayer packet path calls this with the server's snapshot.
void repgame_loot_chest_contents( RepGameState &state, const ChestContents &contents );