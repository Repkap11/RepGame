#pragma once

#include <stdio.h>
#include <string>

#include "common/Platforms.hpp"
#include "common/Logging.hpp"

struct RepGameState;

// The RepGameState struct and its named pieces (CameraState, UiState,
// VrState, ...) live under include/common/game/ — this header is the API.
#include "common/game/game_state.hpp"
#include "common/mob_types.hpp" // MobTypeDef
#include "common/chest.hpp"     // ChestContents
#include "common/furnace.hpp"   // FurnaceContents

#define DEBUG 1

class RepGame {
    RepGameState globalGameState;

    BlockID change_block( int place, BlockState blockState );
    void break_selected_block( );
    unsigned char getPlacedRotation( BlockID blockID ) const;
    void initializeGameState( const char *world_name );
    void add_to_hotbar( bool alsoSelect, ItemID itemId );
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
    // Shared body of draw()/draw_vr(): everything once per frame, then the
    // world-draw section once per view (flat: a single pass with the screen
    // projection and interpolated camera).
    void draw_frame( float alpha, const RenderView *views, int num_views );
    // Applies vr.input (edge-detected against vr.prev_input) onto the shared
    // Input fields — movement stick, jump/sneak/sprint, hotbar steps, mouse
    // buttons, snap/smooth turn. Called at the top of tick() in VR.
    void process_vr_input( );
    // tick() phases — called in this order from tick(). Gameplay phases live
    // in game_tick.cpp, harness-only phases in game_harness.cpp, VR phases
    // in vr.cpp.
    // True while the tracked head pose owns the camera (vr.active and no
    // capture/fly harness pin) — checked at both VR use sites in tick().
    bool vr_drives_camera( ) const;
    void update_vr_ui_pointer( );
    void apply_vr_head_angles( );
    void advance_clocks( );
    void resolve_pending_spawn( );
    void apply_harness_camera_pins( );
    void process_hotbar_scroll( );
    void close_out_of_reach_containers( );
    void update_block_selection( );
    void apply_mouse_look( );
    void update_break_progress( );
    void recenter_mouse( );
    void apply_harness_input_overrides( );
    void tick_vitals( );
    void tick_entities( );
    void tick_fly_flight( );
    void tick_input_latches( );
    void tick_harness_reporting( );
    // The UI draw sequence shared by the flat tail and draw_vr_ui: atlas
    // bind + CLAMP wrap, depth/stencil clear, ui_overlay + held item +
    // VR pointer marker. Caller owns the FBO/bind/blend state. draw_imgui
    // draws the ImGui debug overlay behind the pause menu (flat path only;
    // VR passes false).
    void draw_ui_pass( const glm::dvec3 &render_pos, float daylight, bool draw_imgui, float alpha );

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
    void cleanup( );
    // Writes current position/inventory/mode/clock to player.dat. Called by
    // cleanup() and, on WASM, periodically so crashes don't lose player state.
    void save_player_data( );
    Input &getInputState( );
    GameMode getGameMode( ) const;
    // True while hostile mobs may target the local player — not peaceful,
    // not no-clip, not dead. Drives MobTickInput.player_targetable and the
    // NET_PLAYER_FLAG_TARGETABLE sent to other clients.
    static bool player_targetable( const RepGameState &state );

    void changeSize( int x, int y );
    void get_screen_size( int *width, int *height ) const;
    bool shouldExit( ) const;
    bool should_lock_pointer( ) const;
    static bool supportsAnisotropic( );
    // True when REPGAME_CAPTURE is set (deterministic screenshot capture mode).
    static bool capture_mode( );
    // True when REPGAME_FLY is set (scripted no-clip flight + FLYMET metrics).
    static bool fly_mode( );
    // Survival pickup shared by block-break and mob drops: hotbar first
    // (stacking onto matching slots), overflow to the survival inventory.
    // Hotbar first, survival inventory for the overflow. Returns the count
    // that didn't fit (0 when everything was given).
    static int give_item_to_player( RepGameState &state, ItemID item, int quantity );
    // Non-mutating capacity check matching give_item_to_player's targets.
    static int inventory_space_for_item( RepGameState &state, ItemID item );
    // Mob-vs-block target resolution for the crosshair ray. Sets
    // block_selection.target_mob/target_mob_dist and, when the mob is nearer
    // than the selected block cell (or no block is selected), clears
    // selectionInBounds/floatSelectionInBounds/fluidSelectionInBounds so no
    // block outline (or place/pick through the mob) happens. Static so
    // rep_tests can drive it.
    static void resolve_target_mob( RepGameState &state, const glm::dvec3 &ray_origin );
    // A hostile mob's melee hit lands here: knockback away from `from`
    // plus survival-mode health damage via player_damage.
    static void player_hurt( RepGameState &state, const glm::dvec3 &from, float damage );
    // Applies `hp` damage (half-hearts) to the local player in survival:
    // gated by dead/creative and the hurt-cooldown i-frame window that
    // paces every periodic source. Returns whether damage landed — callers
    // play their sound only then. Reaching 0 opens the death screen.
    static bool player_damage( RepGameState &state, float hp );
    // Heals the local player by `hp` half-hearts (clamped to max), e.g. food.
    static void player_heal( RepGameState &state, float hp );
    // Death screen -> respawn at the world spawn with full vitals,
    // inventory kept, reusing the spawn-column snap.
    void respawn( );
    // Spawn a killed mob's drops at `pos` (used by the local kill path and
    // the owner-side ENTITY_ACTION HIT path; item spawns sync via phase 1).
    static void spawn_mob_drops( RepGameState &state, const MobTypeDef &def, const glm::dvec3 &pos );
    static void spawn_mob_shear_drop( RepGameState &state, const MobTypeDef &def, const glm::dvec3 &pos );
    // Chest open/close (sends CONTAINER_OPEN/CLOSE when connected).
    static void open_chest( RepGameState &state, const glm::ivec3 &pos );
    static void close_chest( RepGameState &state );
    // Furnace open/close (sends CONTAINER_OPEN/CLOSE when connected).
    static void open_furnace( RepGameState &state, const glm::ivec3 &pos );
    static void close_furnace( RepGameState &state );
    // mp_test harness: move the pinned camera (re-aims the REPGAME_FLY
    // origin so the pin sticks, or just teleports the camera otherwise).
    static void mp_test_goto( RepGameState &state, const glm::dvec3 &pos );

    int rep_tests_start( );
};

// Moves every non-empty slot of a container into the player's inventories
// (survival first, hotbar for the overflow). Used for container-break
// looting; the multiplayer CONTAINER_CONTENTS loot path calls this with
// the server's authoritative snapshot.
void repgame_loot_container( RepGameState &state, const InventorySlot *slots, int count );

// Right-click use of a bucket item (BUCKET/WATER_BUCKET/LAVA_BUCKET) on the
// current block selection: an empty bucket scoops the fluid source under
// fluidSelectionInBounds; a filled one pours its fluid at pos_fluid (a
// flowing hit), pos_fluid_create, or pos_create. Queues the world change as
// a PlayerBlockPlacedEvent and swaps the held item in survival (creative
// keeps it). Returns true when a bucket action happened; a free function so
// rep_tests can drive it.
bool repgame_use_bucket( RepGameState &state );

// Right-click with the bow held: releases an arrow along the camera look
// direction at full draw speed. Survival consumes one ARROW (hotbar first,
// then the survival inventory); creative shoots free. False when no arrow
// could fly. A free function so rep_tests can drive it.
bool repgame_fire_arrow( RepGameState &state );

// Right-click with a food item held (item_food_heal > 0) and health below
// max: eats one — heals `food_heal` half-hearts instantly (beta-style, no
// eat animation), consumes one in survival, plays the eat sound. False
// when the held item isn't food or health is already full. A free
// function so rep_tests and the mp-test harness can drive it.
bool repgame_eat_food( RepGameState &state );

// Right-click a button: powers it for its press duration (stone ~1 s,
// wood ~1.5 s on the redstone clock), then releases it. The press write is
// instant; the release is re-queued every press so a saved-pressed button
// still pops out. A free function so rep_tests can drive it.
void repgame_press_button( RepGameState &state, const glm::ivec3 &pos );

// Explosion blast on the local player: distance-falloff damage
// (PLAYER_EXPLOSION_MAX_DAMAGE at the center fading to 0 at `radius`)
// plus a shove away from the blast. Called from TntExplodeEvent for local
// blasts and from the inbound EXPLOSION frame for remote ones.
void repgame_explosion_affect_player( RepGameState &state, const glm::dvec3 &center, float radius );

// Per-tick vitals upkeep (the body of RepGame::tick_vitals): i-frame
// countdown, breath drain + drowning underwater, lava/fire burns, and
// peaceful regen. A free function so rep_tests can drive it.
void repgame_tick_vitals( RepGameState &state );

// Death-screen Respawn (the body of RepGame::respawn): full vitals, menu
// closed, player back on the world-spawn column — inventory kept.
void repgame_respawn( RepGameState &state );

// Right-clicking a bed: records it as the respawn point, and in the night
// half of the day sleeps the world clock to the next dawn (refused while
// hostiles are near — the spawn point is set regardless). True when the
// clock advanced. A free function so rep_tests can drive it.
bool repgame_try_sleep( RepGameState &state, const glm::ivec3 &bed_pos );

// The body of RepGame::resolve_pending_spawn: while spawn_pending, wait
// for the spawn column (the recorded bed when spawn_point_set, else (0,0))
// to report generated data, then snap the player onto it — the bed top,
// or the column's topmost colliding block. A missing/obstructed bed clears
// the point back to the world spawn. A free function so rep_tests can
// drive it.
void repgame_resolve_pending_spawn( RepGameState &state );

// Minecart riding (src/common/game/minecart_ride.cpp): mount the cart by
// id (fails when it's gone or already ridden), dismount to the first
// free spot beside/on the cart, and the per-tick follow that seats the
// camera on the ridden cart (or clears riding_cart when it's gone).
// Free functions so rep_tests and the harness can drive them.
bool repgame_cart_mount( RepGameState &gs, uint32_t cart_id );
void repgame_cart_dismount( RepGameState &gs );
void repgame_cart_ride_follow( RepGameState &gs );

// Sign editor (src/common/game/sign_edit.cpp): modal four-line text box
// opened by right-clicking a standing/wall sign. Free functions
// so rep_tests and the harness can drive them.
void repgame_open_sign_editor( RepGameState &state, const glm::ivec3 &pos );
// commit=false discards the edit (broken sign, unloaded chunk, out of
// reach); commit=true writes it through World::set_sign_text (+ a
// SIGN_TEXT message when connected).
void repgame_close_sign_editor( RepGameState &state, bool commit );
void repgame_commit_sign_edit( RepGameState &state );
// tick() phase: drains the Input text counters and applies the close
// checks (Escape/Enter handled inside sign_edit_apply_input).
void repgame_tick_sign_editor( RepGameState &state );
// Pure editor key handling — returns true when the edit should commit and
// close (Enter on the last line or Escape). rep_tests drive it directly.
bool sign_edit_apply_input( SignEditUi &edit, const Input &in );
// The editor panel draw — called from draw_ui_pass in the menu layer.
void repgame_draw_sign_editor( RepGameState &state, const Renderer &renderer, Shader &shader, FontRenderer &font, const glm::mat4 &mvp_ui );

// Chat console (src/common/game/chat_ui.cpp): a non-modal input bar +
// scrollback above the hotbar. 't'/Tab opens it, '/' opens it prefilled;
// refused while the sign editor, pause menu, or inventory is up. Free
// functions so rep_tests and the harness can drive them.
void repgame_open_chat( RepGameState &state, const std::string &prefix );
void repgame_close_chat( RepGameState &state );
// tick() phase: consumes input.chat_open_request, drains the Input text
// counters, and submits on Enter. Runs right after
// repgame_tick_sign_editor (they're mutually exclusive).
void repgame_tick_chat( RepGameState &state );
// Pure input handling — `submitted` receives the line on Enter; the
// return value says the box should close (Enter or Escape). rep_tests
// drive it directly.
bool chat_edit_apply_input( ChatUi &chat, const Input &in, std::string *submitted );
// One submitted line: a leading '/' runs a slash command; anything else
// goes out as PLAYER chat in multiplayer, or straight into the local log
// in single player.
void chat_submit( RepGameState &state, const std::string &line );
// The chat log + input bar draw — called from draw_ui_pass in the HUD
// layer before the first text flush.
void repgame_draw_chat( RepGameState &state, const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp_ui );
// Slash-command dispatcher (src/common/game/chat_commands.cpp): runs
// `cmdline` (no leading slash) against the command table; command output
// lands in state.chat as INFO/ERROR lines.
void chat_run_command( RepGameState &state, const std::string &cmdline );
