#pragma once

// The "Shadows" pause-menu setting: FAST uploads the raw chamfer light
// field; SMOOTH applies the air-gated 3x3x3 upload-time blur (light.cpp).
// REPGAME_LIGHT_SMOOTH overrides it when set.
enum ShadowQuality { SHADOW_FAST = 0, SHADOW_SMOOTH = 1, SHADOW_QUALITY_LAST };

class Input {
    void processMovement( );

  public:
    struct {
        float sizeH;
        float angleH;
        bool jumpPressed;
        bool sneakPressed;
    } movement;
    struct {
        struct {
            int left;
            int right;
            int middle;
            bool left_click_handled;
            bool right_click_handled;
            bool middle_click_handled;
            // Where each held press began: set to inventory_open on the down
            // edge, cleared on the up edge. World actions (mine/place/pick)
            // ignore presses that began over the UI, and inventory clicks
            // ignore presses that began in the world.
            bool left_pressed_in_ui;
            bool right_pressed_in_ui;
            bool middle_pressed_in_ui;
        } buttons;
        struct {
            int x;
            int y;
            int wheel_counts;
        } currentPosition;
        struct {
            int x;
            int y;
            int wheel_counts;
        } previousPosition;
        struct {
            int x;
            int y;
        } absPosition;
        // Low-pass filtered look deltas, persisted across ticks.
        float smoothed_dx;
        float smoothed_dy;
    } mouse;
    bool exitGame;
    int click_delay_left;
    int click_delay_right;
    int click_delay_middle;
    bool debug_mode;
    bool inventory_open;
    // Pause menu open (Escape / Android menu button). Releases the pointer
    // like inventory_open and routes clicks to PauseMenu::hitTest.
    bool menu_open;
    bool player_flying;
    bool player_sprinting;
    bool no_clip;
    WorldDrawQuality worldDrawQuality;
    ShadowQuality shadow_quality = SHADOW_SMOOTH;
    // Active horizontal render distance in chunks [1, RENDER_RADIUS_MAX];
    // adjusted live with =/- (numpad +/- too), applied to ChunkLoader each
    // frame. CHUNK_RADIUS_* remains the compiled-in torus capacity.
    int render_distance;
    // Armed on the Q down edge; consumed by the inventory tick to throw one
    // item (Ctrl: the whole stack). The release must not arm it.
    bool drop_item;
    // Q's physical held state, tracked separately so OS key-repeat (which
    // arrives as pressed while held) can't re-arm drop_item. Held repeats
    // instead fire through drop_item_delay at a fixed tick rate.
    bool drop_item_held;
    // Ticks until the next held-Q repeat throw may fire.
    int drop_item_delay;
    // Ctrl modifier state, sampled like shift_held — Ctrl+Q throws the whole
    // selected stack instead of one item.
    bool ctrl_held;
    bool screenshot_requested;
    bool toggle_game_mode;
    // Set by the 'v' key; consumed by the SDL frame loop to flip
    // SDL_GL_SetSwapInterval (hardware vsync) at runtime.
    bool toggle_vsync;
    // Set by the 'g' key; consumed by imgui_overlay_draw to show/hide the
    // debug overlay at runtime.
    bool toggle_imgui;
    // Set by the 'n' key (debug); consumed by RepGame::tick to spawn a mob
    // in front of the player.
    bool spawn_mob;
    bool shift_held;
    // Accumulated PageUp/PageDown time-of-day nudges (in day-fraction steps);
    // consumed by RepGame each frame and applied to world_time.
    int time_skip_hours;
    // Set by touch platforms when the screen is tapped. Consumed once per tick:
    // acts as a click at absPosition when the inventory is open, or a hotbar
    // slot tap-select when it is closed.
    bool screen_tap_pending;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM )
    void mouseInput( int button, int state );
    void set_enable_mouse( bool enable );
    void keysInput( const SDL_Keycode key, const bool pressed );
    void mouseWheel( const int x_delta, const int y_delta );
    void mousePosition( int x, int y );
#else
    void positionHMove( float sizeH, float angleH );
    void setJumpPressed( int jumpPressed );
    void setButtonState( int left, int middle, int right );
    void onInventoryClicked( );
#endif
    // Escape (desktop) / Menu button (Android): closes the inventory if it's
    // open, otherwise toggles the pause menu.
    void onMenuClicked( );
    void lookMove( int x, int y );
};