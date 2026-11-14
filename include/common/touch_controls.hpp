#pragma once

#include <stdint.h>

#include <glm/mat4x4.hpp>

#include "common/RenderChain.hpp"
#include "common/ui_overlay.hpp" // UIOverlayVertex / UIOverlayInstance

// On-screen touch controls (the shared C++ port of Android's
// UIOverlayView): a joystick, jump/sneak/break/pick/place/inv/menu
// buttons drawn through the in-game UI, plus the inventory-open variant
// (Close / game-mode / creative page arrows). Geometry comes from the
// central layout pass (UiLayoutRects::touch_*) — nothing here derives a
// screen position.
//
// Platform event loops feed touches through RepGame::touch_event ->
// repgame_touch_event -> TouchControls::event; the module is inert until
// input.touch_ui is set (Android at init, WASM from JS, desktop via the
// pause-menu "Touch Controls" toggle or the `touchui` rep-dbg verb).

class Input;
class FontRenderer;
class Renderer;
class Shader;
class Texture;
class VertexBufferLayout;
struct RepGameState;
struct UiLayoutRects;

enum TouchPhase { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP, TOUCH_CANCEL };

// Look-drag sensitivity (the Android side's ANDROID_PAN_SENSITIVITY).
constexpr float TOUCH_LOOK_SENSITIVITY = 1.25f;
// Tap slop: a touch whose total travel stays under this counts as a tap,
// not a drag. Fraction of the short screen edge (~8dp on a 640p-class
// display, matching Android's ViewConfiguration touch slop).
constexpr float TOUCH_TAP_SLOP_FRACTION = 0.012f;

// Everything one touch event needs, kept free of RepGameState so tests
// can drive the module directly. x/y event coords are window pixels,
// top-left origin, y down (the same space as mouse.absPosition).
struct TouchContext {
    Input &input;
    const UiLayoutRects &rects;
    float screen_w;
    float screen_h;
    // game_mode == GameMode_Creative — the inventory page buttons only
    // exist in creative.
    bool creative;
};

class TouchControls {
  public:
    void init( const VertexBufferLayout &vbl_vertex, const VertexBufferLayout &vbl_instance );
    // One pointer event. ids are int64_t like Android's MotionEvent
    // pointer ids; -1 is "none" internally so platform ids just can't
    // collide with empty state.
    void event( const TouchContext &ctx, TouchPhase phase, int64_t id, float x, float y );
    // Release every held control and clear the pointer ids (menu opened,
    // touch UI switched off, window lost the gesture).
    void cancel_all( Input &input );
    // Once per frame: if the menu just opened or touch_ui turned off,
    // cancel_all. Sneak is a UI toggle, deliberately left alone.
    void tick( Input &input );
    // The control quads + labels, drawn in draw_ui_pass before the pause
    // menu's text layer. Hidden while !touch_ui or menu_open; the
    // gameplay set swaps for the inventory set while inventory_open.
    void draw( const Renderer &renderer, Shader &shader, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp, const RepGameState &state );
    void cleanup( );

  private:
    int64_t move_id = -1;  // joystick pointer
    int64_t look_id = -1;  // look-drag pointer (tap on short release)
    int64_t jump_id = -1;
    int64_t left_id = -1;  // Break
    int64_t mid_id = -1;   // Pick
    int64_t right_id = -1; // Place
    int64_t tap_id = -1;   // tap-tracked pointer (menu/inventory taps)
    float look_fx = 0.0f;
    float look_fy = 0.0f; // last look position, window px
    float look_down_x = 0.0f;
    float look_down_y = 0.0f; // look drag start, for the tap slop check
    float tap_down_x = 0.0f;
    float tap_down_y = 0.0f;
    // Joystick knob center in centered y-up UI coords (valid while
    // move_id != -1; the draw snaps back to the joystick center).
    float knob_x = 0.0f;
    float knob_y = 0.0f;
    bool prev_menu_open = false;
    bool prev_touch_ui = false;
    RenderChain<UIOverlayVertex, UIOverlayInstance> quads;

    // push the held Break/Pick/Place ids into mouse.buttons.
    void apply_buttons( Input &input ) const;
    // The joystick math (Java's handleHMove): x,y in centered y-up
    // coords; updates the knob and calls positionHMove.
    void hmove( const TouchContext &ctx, float x, float y );
};

// Platform entry point (RepGame::touch_event calls this on its state):
// builds the TouchContext and dispatches — a no-op unless
// state.input.touch_ui.
void repgame_touch_event( RepGameState &state, TouchPhase phase, int64_t id, float x, float y );
