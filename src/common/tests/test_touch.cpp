// The shared touch controls (touch_controls.cpp): pointer routing,
// joystick math, look drags vs taps, and cancel/menu semantics, driven
// through TouchContext on a plain Input + UiLayoutRects — no RepGameState
// or GL needed. Coordinates are window pixels (top-left origin, y down)
// like the platform loops deliver.

#include <math.h>

#include "test_util.hpp"
#include "common/font_renderer.hpp"
#include "common/ui_layout.hpp"
#include "common/touch_controls.hpp"
#include "common/input.hpp"

static constexpr float SCR_W = 1920.0f;
static constexpr float SCR_H = 1080.0f;

// Window-px conversion for test-side point picking: the rects are
// centered y-up; touches are top-left y-down.
static void win_xy( const UiRect &r, float *x, float *y ) {
    *x = r.center_x( ) + SCR_W * 0.5f;
    *y = SCR_H * 0.5f - r.center_y( );
}

int test_touch( ) {
    int failures = 0;
    auto is_true = [ & ]( const char *what, bool ok ) {
        pr_test( "  %-58s %s", what, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    auto near_to = [ & ]( const char *what, float got, float want, float tol ) {
        const bool ok = fabsf( got - want ) <= tol;
        pr_test( "  %-58s got=%-9.3f want=%-9.3f  %s", what, got, want, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    UiLayout layout;
    FontRenderer font;
    layout.init( font );
    UiLayoutInput lin;
    lin.screen_w = SCR_W;
    lin.screen_h = SCR_H;
    lin.hotbar_slab_w = 500.0f;
    lin.hotbar_slab_h = 60.0f;
    lin.cell_stride = 55.0f;
    lin.label_h = 18.0f;
    lin.surv_slab_w = 9.1f * 55.0f;
    lin.surv_slab_h = 3.1f * 55.0f;
    lin.surv_stride = 55.0f;
    lin.creative_slab_w = 720.0f;
    lin.creative_panel_w = SCR_W * 0.75f;
    lin.creative_panel_h = 0.75f * SCR_H;
    lin.creative_slab_h = 0.75f * lin.creative_panel_h;
    lin.creative_stride = lin.creative_slab_h / 6.1f;
    lin.creative_floor = -SCR_H * 0.5f + 0.1f * SCR_H + 6.0f;
    layout.compute( lin );
    const UiLayoutRects &rects = layout.rects( );

    Input input = { };
    TouchControls touch;
    const TouchContext ctx = { input, rects, SCR_W, SCR_H, true };

    float px, py;

    // --- Joystick: drag right -> sizeH ~1, angleH ~90 ('d' = +90) ------
    win_xy( rects.touch_joystick, &px, &py );
    const float joy_x = px, joy_y = py;
    touch.event( ctx, TOUCH_DOWN, 1, joy_x, joy_y );
    touch.event( ctx, TOUCH_MOVE, 1, joy_x + rects.touch_joystick.width, joy_y );
    near_to( "joystick drag right: sizeH ~1", input.movement.sizeH, 1.0f, 0.05f );
    near_to( "joystick drag right: angleH ~90", input.movement.angleH, 90.0f, 1.0f );
    touch.event( ctx, TOUCH_UP, 1, joy_x + rects.touch_joystick.width, joy_y );
    near_to( "joystick release: sizeH 0", input.movement.sizeH, 0.0f, 0.001f );

    // Drag up -> forward (angleH ~0).
    touch.event( ctx, TOUCH_DOWN, 1, joy_x, joy_y );
    touch.event( ctx, TOUCH_MOVE, 1, joy_x, joy_y - rects.touch_joystick.width );
    near_to( "joystick drag up: angleH ~0", fabsf( input.movement.angleH ), 0.0f, 1.0f );
    touch.event( ctx, TOUCH_UP, 1, joy_x, joy_y - rects.touch_joystick.width );

    // --- Look drag -> mouse.currentPosition moves, no tap --------------
    const int look_x0 = input.mouse.currentPosition.x;
    const int look_y0 = input.mouse.currentPosition.y;
    touch.event( ctx, TOUCH_DOWN, 2, 960.0f, 400.0f );
    touch.event( ctx, TOUCH_MOVE, 2, 1060.0f, 430.0f );
    is_true( "look drag: currentPosition.x += ~125", input.mouse.currentPosition.x - look_x0 >= 120 && input.mouse.currentPosition.x - look_x0 <= 130 );
    is_true( "look drag: currentPosition.y += ~37", input.mouse.currentPosition.y - look_y0 >= 34 && input.mouse.currentPosition.y - look_y0 <= 40 );
    touch.event( ctx, TOUCH_UP, 2, 1060.0f, 430.0f );
    is_true( "look drag: no tap", !input.screen_tap_pending );

    // --- Short tap on empty world -> screen_tap_pending ----------------
    touch.event( ctx, TOUCH_DOWN, 3, 960.0f, 400.0f );
    touch.event( ctx, TOUCH_UP, 3, 962.0f, 402.0f );
    is_true( "short tap: screen_tap_pending", input.screen_tap_pending );
    is_true( "short tap: absPosition set", input.mouse.absPosition.x == 962 && input.mouse.absPosition.y == 402 );
    input.screen_tap_pending = false;

    // --- TOUCH_CANCEL never taps ---------------------------------------
    touch.event( ctx, TOUCH_DOWN, 3, 960.0f, 400.0f );
    touch.event( ctx, TOUCH_CANCEL, 3, 961.0f, 401.0f );
    is_true( "cancel: no tap", !input.screen_tap_pending );
    // Stuck-pointer hygiene: the cancelled look finger is released.
    touch.event( ctx, TOUCH_MOVE, 3, 1200.0f, 500.0f );
    is_true( "cancel: pointer cleared", input.mouse.currentPosition.x - look_x0 <= 130 );

    // --- Break/Pick/Place hold -> mouse buttons ------------------------
    win_xy( rects.touch_break, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 4, px, py );
    is_true( "break held: buttons.left", input.mouse.buttons.left == 1 );
    touch.event( ctx, TOUCH_UP, 4, px, py );
    is_true( "break release: buttons.left 0", input.mouse.buttons.left == 0 );

    win_xy( rects.touch_place, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 4, px, py );
    is_true( "place held: buttons.right", input.mouse.buttons.right == 1 );
    touch.event( ctx, TOUCH_UP, 4, px, py );
    is_true( "place release: buttons.right 0", input.mouse.buttons.right == 0 );

    // --- Two fingers concurrently: joystick + look ---------------------
    const int lx = input.mouse.currentPosition.x;
    touch.event( ctx, TOUCH_DOWN, 5, joy_x, joy_y );          // finger 5: joystick
    touch.event( ctx, TOUCH_DOWN, 6, 960.0f, 400.0f );        // finger 6: look
    touch.event( ctx, TOUCH_MOVE, 5, joy_x + rects.touch_joystick.width, joy_y );
    touch.event( ctx, TOUCH_MOVE, 6, 1000.0f, 400.0f );
    near_to( "two fingers: joystick sizeH ~1", input.movement.sizeH, 1.0f, 0.05f );
    is_true( "two fingers: look moved", input.mouse.currentPosition.x - lx >= 45 );
    touch.event( ctx, TOUCH_UP, 5, joy_x + rects.touch_joystick.width, joy_y );
    near_to( "two fingers: stick released alone", input.movement.sizeH, 0.0f, 0.001f );
    touch.event( ctx, TOUCH_UP, 6, 1000.0f, 400.0f );

    // --- Second finger on a held control is ignored --------------------
    win_xy( rects.touch_jump, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 7, px, py );
    touch.event( ctx, TOUCH_DOWN, 8, px, py ); // finger 8 on held Jump
    is_true( "2nd finger on held jump ignored", input.movement.jumpPressed );
    touch.event( ctx, TOUCH_UP, 8, px, py );   // releasing it must not drop jump
    is_true( "held jump survives stray up", input.movement.jumpPressed );
    touch.event( ctx, TOUCH_UP, 7, px, py );
    is_true( "jump released", !input.movement.jumpPressed );

    // --- Sneak toggles --------------------------------------------------
    win_xy( rects.touch_sneak, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 9, px, py );
    touch.event( ctx, TOUCH_UP, 9, px, py );
    is_true( "sneak tap: toggled on", input.movement.sneakPressed );
    touch.event( ctx, TOUCH_DOWN, 9, px, py );
    touch.event( ctx, TOUCH_UP, 9, px, py );
    is_true( "sneak tap: toggled off", !input.movement.sneakPressed );

    // --- Inventory button -> inventory_open toggle ----------------------
    win_xy( rects.touch_inventory, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 10, px, py );
    is_true( "inv button: inventory opens", input.inventory_open );

    // Inventory set: Close at the same spot closes it; the mode button
    // arms the game-mode toggle.
    win_xy( rects.touch_inv_mode, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 10, px, py );
    is_true( "mode button: toggle_game_mode", input.toggle_game_mode );
    input.toggle_game_mode = false;
    win_xy( rects.touch_inv_close, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 10, px, py );
    is_true( "close button: inventory closes", !input.inventory_open );

    // --- Chat button -> chat_open_request armed --------------------------
    win_xy( rects.touch_chat, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 10, px, py );
    is_true( "chat button: chat_open_request", input.chat_open_request == 1 );
    input.chat_open_request = 0;

    // --- Menu button -> menu_open + controls released -------------------
    win_xy( rects.touch_jump, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 11, px, py ); // hold jump
    win_xy( rects.touch_menu, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 12, px, py ); // tap Menu
    is_true( "menu button: menu opens", input.menu_open );
    is_true( "menu open: jump released", !input.movement.jumpPressed );
    is_true( "menu open: sizeH cleared", input.movement.sizeH == 0.0f );

    // While the menu is up every touch is a tap.
    touch.event( ctx, TOUCH_DOWN, 13, 500.0f, 500.0f );
    touch.event( ctx, TOUCH_UP, 13, 501.0f, 501.0f );
    is_true( "menu open: touch taps", input.screen_tap_pending && input.mouse.absPosition.x == 501 );
    input.screen_tap_pending = false;

    // tick(): a menu opened outside the touch path still drops held
    // controls on the next frame.
    input.menu_open = false;
    win_xy( rects.touch_jump, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 14, px, py );
    input.menu_open = true; // e.g. Escape
    touch.tick( input );
    is_true( "menu edge: jump released", !input.movement.jumpPressed );
    input.menu_open = false;
    touch.tick( input );

    // tick(): switching touch_ui off drops held controls too.
    input.touch_ui = true;
    touch.tick( input ); // latch prev_touch_ui before the falling edge
    win_xy( rects.touch_break, &px, &py );
    touch.event( ctx, TOUCH_DOWN, 15, px, py );
    is_true( "break held before touch_ui off", input.mouse.buttons.left == 1 );
    input.touch_ui = false;
    touch.tick( input );
    is_true( "touch_ui off: buttons released", input.mouse.buttons.left == 0 );

    layout.destroy( );
    return failures;
}
