// The shared on-screen touch controls — a 1:1 port of Android's
// UIOverlayView (onTouch / handleHMove / handleLook / releaseAllControls)
// so WASM-on-mobile can share it. Behaviour notes live with the code;
// layout lives in ui_layout.cpp (UiLayoutRects::touch_*).

#include <math.h>

#include "common/RepGame.hpp"
#include "common/touch_controls.hpp"
#include "common/ui_layout.hpp"
#include "common/ui_overlay_buffers.hpp"
#include "common/font_renderer.hpp"

// Z order within the UI depth pass: above the HUD/vitals (0.5) and below
// the pause menu (0.6+, which also depth-clears before it draws).
#define ORDER_Z_TOUCH 0.55f
#define ORDER_Z_TOUCH_KNOB 0.56f

void TouchControls::init( const VertexBufferLayout &vbl_vertex, const VertexBufferLayout &vbl_instance ) {
    this->quads.init( vbl_vertex, vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
}

// Same instanced-quad helper as the pause menu.
static UIOverlayInstance &add_touch_quad( RenderChain<UIOverlayVertex, UIOverlayInstance> &chain, //
                                        float x, float y, float w, float h, float z,            //
                                        float r, float g, float b, float a ) {
    auto pair = chain.create_instance( );
    UIOverlayInstance &quad = pair.second;
    quad.screen_x = x;
    quad.screen_y = y;
    quad.screen_z = z;
    quad.width = w;
    quad.height = h;
    quad.is_block = 0;
    quad.is_isometric = 0;
    quad.tint[ 0 ] = r;
    quad.tint[ 1 ] = g;
    quad.tint[ 2 ] = b;
    quad.tint[ 3 ] = a;
    return quad;
}

void TouchControls::apply_buttons( Input &input ) const {
    input.setButtonState( this->left_id != -1 ? 1 : 0, this->mid_id != -1 ? 1 : 0, this->right_id != -1 ? 1 : 0 );
}

// The joystick, in centered y-up UI coords. angleH convention matches
// the desktop WASD map in Input::processMovement: drag up = 0 (forward),
// right = +90, left = -90, down = 180 — the Java math carried through
// the y-flip.
void TouchControls::hmove( const TouchContext &ctx, const float x, const float y ) {
    const UiRect &joy = ctx.rects.touch_joystick;
    const float cx = joy.center_x( );
    const float cy = joy.center_y( );
    const float dx = x - cx;
    const float dy = y - cy;
    const float d = sqrtf( dx * dx + dy * dy );
    // Java's maxDist = moveRadius - fingerRadius = r - r/3; the layout
    // emits a 2r-wide joystick, so that's width/3 here.
    const float max_dist = joy.width / 3.0f;
    float dist_x = fabsf( dx );
    float dist_y = fabsf( dy );
    if ( dist_x > max_dist ) {
        dist_x = max_dist;
    }
    if ( dist_y > max_dist ) {
        dist_y = max_dist;
    }
    const float dist = sqrtf( dist_x * dist_x + dist_y * dist_y );
    const float inv_d = d > 0.0f ? 1.0f / d : 0.0f;
    this->knob_x = cx + dx * inv_d * dist_x;
    this->knob_y = cy + dy * inv_d * dist_y;
    const float angle_h = d > 0.0f ? 90.0f - atan2f( dy, dx ) * ( 180.0f / static_cast<float>( M_PI ) ) : 0.0f;
    ctx.input.positionHMove( max_dist > 0.0f ? dist / max_dist : 0.0f, angle_h );
}

void TouchControls::cancel_all( Input &input ) {
    // Java's releaseAllControls: recenter the stick and drop every held
    // pointer. Sneak is a toggle mirrored in movement.sneakPressed —
    // deliberately untouched.
    this->move_id = -1;
    this->look_id = -1;
    this->tap_id = -1;
    input.positionHMove( 0.0f, 0.0f );
    if ( this->jump_id != -1 ) {
        this->jump_id = -1;
        input.setJumpPressed( 0 );
    }
    if ( this->left_id != -1 || this->mid_id != -1 || this->right_id != -1 ) {
        this->left_id = this->mid_id = this->right_id = -1;
        input.setButtonState( 0, 0, 0 );
    }
}

void TouchControls::tick( Input &input ) {
    if ( ( input.menu_open && !this->prev_menu_open ) || ( !input.touch_ui && this->prev_touch_ui ) ) {
        this->cancel_all( input );
    }
    this->prev_menu_open = input.menu_open;
    this->prev_touch_ui = input.touch_ui;
}

void TouchControls::event( const TouchContext &ctx, const TouchPhase phase, const int64_t id, const float x, const float y ) {
    Input &input = ctx.input;
    const UiLayoutRects &r = ctx.rects;
    const float slop = TOUCH_TAP_SLOP_FRACTION * fminf( ctx.screen_w, ctx.screen_h );
    // Window px (top-left origin, y down) -> centered y-up UI space, the
    // same conversion PauseMenu::hitTest applies.
    const float ux = x - ctx.screen_w * 0.5f;
    const float uy = ctx.screen_h * 0.5f - y;

    if ( phase == TOUCH_DOWN ) {
        // While the pause menu (or modal text capture) owns the screen
        // every touch is a tap delivered through the same path as
        // inventory clicks.
        if ( input.menu_open || input.text_input_active ) {
            this->tap_id = id;
            this->tap_down_x = x;
            this->tap_down_y = y;
            input.mouse.absPosition.x = static_cast<int>( x );
            input.mouse.absPosition.y = static_cast<int>( y );
            return;
        }
        if ( input.inventory_open ) {
            if ( r.touch_inv_close.contains( ux, uy ) ) {
                input.onInventoryClicked( );
            } else if ( r.touch_inv_mode.contains( ux, uy ) ) {
                input.toggle_game_mode = true;
            } else if ( ctx.creative && r.touch_page_prev.contains( ux, uy ) ) {
                // Feeds the wheel counter so the tick applies the same
                // page logic as a desktop scroll (JNI's
                // inventoryPageScrolled).
                input.mouse.currentPosition.wheel_counts -= -1;
            } else if ( ctx.creative && r.touch_page_next.contains( ux, uy ) ) {
                input.mouse.currentPosition.wheel_counts -= 1;
            } else {
                this->tap_id = id;
                this->tap_down_x = x;
                this->tap_down_y = y;
                input.mouse.absPosition.x = static_cast<int>( x );
                input.mouse.absPosition.y = static_cast<int>( y );
            }
            return;
        }
        // Gameplay: a second finger on an already-held control is
        // ignored (the per-control pointer id owns the gesture).
        if ( this->move_id == -1 && r.touch_joystick.contains( ux, uy ) ) {
            this->move_id = id;
            this->knob_x = r.touch_joystick.center_x( );
            this->knob_y = r.touch_joystick.center_y( );
        } else if ( this->jump_id == -1 && r.touch_jump.contains( ux, uy ) ) {
            this->jump_id = id;
            input.setJumpPressed( 1 );
        } else if ( r.touch_sneak.contains( ux, uy ) ) {
            // Sneak toggles on touch — holding it while aiming is
            // impractical (Java's mSneakOn).
            input.movement.sneakPressed = !input.movement.sneakPressed;
        } else if ( this->left_id == -1 && r.touch_break.contains( ux, uy ) ) {
            this->left_id = id;
            this->apply_buttons( input );
        } else if ( this->mid_id == -1 && r.touch_pick.contains( ux, uy ) ) {
            this->mid_id = id;
            this->apply_buttons( input );
        } else if ( this->right_id == -1 && r.touch_place.contains( ux, uy ) ) {
            this->right_id = id;
            this->apply_buttons( input );
        } else if ( r.touch_inventory.contains( ux, uy ) ) {
            input.onInventoryClicked( );
        } else if ( r.touch_chat.contains( ux, uy ) ) {
            // Same down-edge arm the 't' key sets; the chat tick opens
            // the box (refused on Android where repgame_open_chat is a
            // no-op stub).
            input.chat_open_request = 1;
        } else if ( r.touch_menu.contains( ux, uy ) ) {
            input.onMenuClicked( );
            if ( input.menu_open ) {
                this->cancel_all( input );
            }
        } else {
            // Any other touch is a look drag (tap on a short release).
            this->look_id = id;
            this->look_down_x = x;
            this->look_down_y = y;
            this->look_fx = x;
            this->look_fy = y;
        }
        return;
    }
    if ( phase == TOUCH_MOVE ) {
        if ( id == this->move_id ) {
            this->hmove( ctx, ux, uy );
        }
        if ( id == this->look_id ) {
            input.lookMove( static_cast<int>( ( x - this->look_fx ) * TOUCH_LOOK_SENSITIVITY ),
                            static_cast<int>( ( y - this->look_fy ) * TOUCH_LOOK_SENSITIVITY ) );
            this->look_fx = x;
            this->look_fy = y;
        }
        if ( id == this->tap_id ) {
            input.mouse.absPosition.x = static_cast<int>( x );
            input.mouse.absPosition.y = static_cast<int>( y );
        }
        return;
    }
    // TOUCH_UP / TOUCH_CANCEL — cancel is UP without the tap.
    const bool is_up = phase == TOUCH_UP;
    if ( id == this->move_id ) {
        this->move_id = -1;
        this->hmove( ctx, r.touch_joystick.center_x( ), r.touch_joystick.center_y( ) );
    }
    if ( id == this->look_id ) {
        if ( is_up ) {
            const float dx = x - this->look_down_x;
            const float dy = y - this->look_down_y;
            if ( dx * dx + dy * dy <= slop * slop ) {
                // A tap on the world: hotbar slot select under the
                // inventory-closed tick path (or a click at absPosition
                // when the inventory reopens between down and up).
                input.mouse.absPosition.x = static_cast<int>( x );
                input.mouse.absPosition.y = static_cast<int>( y );
                input.screen_tap_pending = true;
            }
        }
        this->look_id = -1;
    }
    if ( id == this->jump_id ) {
        this->jump_id = -1;
        input.setJumpPressed( 0 );
    }
    if ( id == this->left_id ) {
        this->left_id = -1;
        this->apply_buttons( input );
    }
    if ( id == this->mid_id ) {
        this->mid_id = -1;
        this->apply_buttons( input );
    }
    if ( id == this->right_id ) {
        this->right_id = -1;
        this->apply_buttons( input );
    }
    if ( id == this->tap_id ) {
        if ( is_up ) {
            const float dx = x - this->tap_down_x;
            const float dy = y - this->tap_down_y;
            if ( dx * dx + dy * dy <= slop * slop ) {
                input.mouse.absPosition.x = static_cast<int>( x );
                input.mouse.absPosition.y = static_cast<int>( y );
                input.screen_tap_pending = true;
            }
        }
        this->tap_id = -1;
    }
}

void TouchControls::draw( const Renderer &renderer, Shader &shader, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp, const RepGameState &state ) {
    const Input &input = state.input;
    if ( !input.touch_ui || input.menu_open ) {
        return;
    }
    const UiLayoutRects &rects = state.ui.layout.rects( );
    if ( rects.touch_joystick.width <= 0.0f ) {
        return;
    }
    shader.set_uniform_mat4f( "u_MVP", mvp );
    shader.set_uniform1i_texture( "u_Texture", blocksTexture );
    shader.set_uniform1f( "u_HeldBlockLight", 1.0f );
    glEnable( GL_DEPTH_TEST );

    this->quads.clear( );
    const float text_size = rects.touch_jump.height * 0.22f;
    const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const auto button = [ & ]( const UiRect &rect, const char *label, bool pressed ) {
        // Translucent black like the Java paints: 0.35 base, brighter
        // while held / toggled on.
        add_touch_quad( this->quads, rect.left, rect.bottom, rect.width, rect.height, ORDER_Z_TOUCH, 0.0f, 0.0f, 0.0f, pressed ? 0.7f : 0.35f );
        if ( label != nullptr && label[ 0 ] != '\0' ) {
            const float baseline = rect.center_y( ) - font.getCapHeight( text_size ) * 0.5f;
            font.text_append( label, rect.center_x( ) - font.getTextWidth( label, text_size ) * 0.5f, baseline, text_size, white );
        }
    };
    if ( input.inventory_open ) {
        button( rects.touch_inv_close, "Close", false );
        const bool creative = state.game_mode == GameMode_Creative;
        button( rects.touch_inv_mode, creative ? "Creative" : "Survival", false );
        if ( creative ) {
            button( rects.touch_page_prev, "<", false );
            button( rects.touch_page_next, ">", false );
        }
    } else {
        const UiRect &joy = rects.touch_joystick;
        add_touch_quad( this->quads, joy.left, joy.bottom, joy.width, joy.height, ORDER_Z_TOUCH, 0.0f, 0.0f, 0.0f, 0.35f );
        const float knob_size = joy.width / 3.0f;
        const bool held = this->move_id != -1;
        add_touch_quad( this->quads,
                        ( held ? this->knob_x : joy.center_x( ) ) - knob_size * 0.5f,
                        ( held ? this->knob_y : joy.center_y( ) ) - knob_size * 0.5f,
                        knob_size, knob_size, ORDER_Z_TOUCH_KNOB, 0.0f, 0.0f, 0.0f, held ? 0.7f : 0.35f );
        button( rects.touch_jump, "Jump", this->jump_id != -1 );
        button( rects.touch_sneak, "Sneak", input.movement.sneakPressed );
        button( rects.touch_break, "Break", this->left_id != -1 );
        button( rects.touch_pick, "Pick", this->mid_id != -1 );
        button( rects.touch_place, "Place", this->right_id != -1 );
        button( rects.touch_inventory, "Inv", false );
        button( rects.touch_menu, "Menu", false );
        button( rects.touch_chat, "Chat", false );
    }
    this->quads.draw( renderer, shader );
    showErrors( );
}

void TouchControls::cleanup( ) {
    this->quads.clear( );
}

void repgame_touch_event( RepGameState &state, const TouchPhase phase, const int64_t id, const float x, const float y ) {
    if ( !state.input.touch_ui ) {
        return;
    }
    const TouchContext ctx = { state.input, state.ui.layout.rects( ), state.screen.width, state.screen.height,
                               state.game_mode == GameMode_Creative };
    state.ui.touch.event( ctx, phase, id, x, y );
}

void RepGame::touch_event( const TouchPhase phase, const int64_t id, const float x, const float y ) {
    repgame_touch_event( globalGameState, phase, id, x, y );
}
