// RepGame member functions: VR session glue, controller input, and the VR UI quad.

#include <cmath>
#include <ctime>
#include <string>
#include <unistd.h>
#include <chrono>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/renderer/frame_buffer.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/utils/env_config.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/game/game_internal.hpp"
#include "common/utils/angles.hpp"

// VR UI quad placement tunables: REPGAME_VR_UI_WIDTH (meters wide, default
// 1.0; height keeps the screen aspect — the layer texture is eye-sized) and
// REPGAME_VR_UI_DIST (meters from the head, default 1.0). Read once.
float repgame_vr_ui_dist( ) {
    static const float dist = env_float( "REPGAME_VR_UI_DIST", 1.0f );
    return dist;
}
glm::vec2 repgame_vr_ui_size( float screen_w, float screen_h ) {
    static const float width = env_float( "REPGAME_VR_UI_WIDTH", 1.0f );
    return glm::vec2( width, width * screen_h / screen_w );
}
void RepGame::set_vr_active( bool active ) {
    globalGameState.vr.active = active;
    if ( active ) {
        // Entering VR: the body inherits wherever the flat camera was facing.
        globalGameState.vr.body_yaw = globalGameState.camera.angle_H;
        // VR renders twice per frame, so it defaults to LOW
        // (REPGAME_VR_QUALITY, 0..3); the flat preference is kept in
        // saved_quality and restored by the 'r' key/persistence.
        globalGameState.vr.saved_quality = globalGameState.input.worldDrawQuality;
        static const int vr_quality = env_int( "REPGAME_VR_QUALITY", 0 );
        const int q = vr_quality < 0 ? 0 : ( vr_quality > 3 ? 3 : vr_quality );
        globalGameState.input.worldDrawQuality = static_cast<WorldDrawQuality>( q );
    }
    globalGameState.ui.overlay.set_vr_mode( active );
}

void RepGame::set_vr_head( const glm::quat &head, const glm::vec3 &head_pos ) {
    globalGameState.vr.head = head;
    globalGameState.vr.head_pos = head_pos;
}

void RepGame::set_vr_input( const VrControllerInput &in ) {
    globalGameState.vr.input = in;
}

bool RepGame::consume_vr_haptic( ) {
    const bool pending = globalGameState.vr.haptic_pending;
    globalGameState.vr.haptic_pending = false;
    return pending;
}

// True while the tracked head pose owns the camera this tick — capture and
// fly modes pin the camera themselves. NOTE: script.pin_camera is not part
// of the predicate, so VR input keeps being processed while a script pins
// the camera (pre-existing quirk, kept as-is).
bool RepGame::vr_drives_camera( ) const {
    return globalGameState.vr.active && !run_policy().capture.enabled && !run_policy().fly.enabled;
}

// UI quad anchor + laser pointer: opening the inventory (any source) pins
// the panel in LOCAL space in front of wherever the head is looking (yaw
// only); closing it hands it back to head-locked mode. Then the aim ray is
// intersected with the world-locked UI quad so this tick's click handling
// sees this tick's position.
void RepGame::update_vr_ui_pointer( ) {
    VrState &vr = globalGameState.vr;
    const bool inv_open = globalGameState.input.inventory_open;
    if ( inv_open && !vr.prev_inventory_open ) {
        const glm::vec3 f = vr.head * glm::vec3( 0.0f, 0.0f, -1.0f );
        const float h = atan2f( f.x, -f.z );
        vr.ui_orientation = glm::angleAxis( -h, glm::vec3( 0.0f, 1.0f, 0.0f ) );
        vr.ui_position = vr.head_pos + vr.ui_orientation * glm::vec3( 0.0f, -0.15f, -repgame_vr_ui_dist( ) );
        vr.ui_world_locked = true;
    } else if ( !inv_open && vr.prev_inventory_open ) {
        vr.ui_world_locked = false;
    }
    vr.prev_inventory_open = inv_open;

    vr.pointer_on_ui = false;
    if ( inv_open && vr.ui_world_locked && vr.input.aim_valid ) {
        const glm::vec2 ui_size = repgame_vr_ui_size( globalGameState.screen.width, globalGameState.screen.height );
        const glm::vec3 o = vr.input.aim_position;
        const glm::vec3 d = vr.input.aim_orientation * glm::vec3( 0.0f, 0.0f, -1.0f );
        const glm::vec3 n = vr.ui_orientation * glm::vec3( 0.0f, 0.0f, 1.0f );
        const float denom = glm::dot( d, n );
        if ( fabsf( denom ) > 1e-4f ) {
            const float t = glm::dot( vr.ui_position - o, n ) / denom;
            if ( t > 0.0f ) {
                const glm::vec3 q = glm::inverse( vr.ui_orientation ) * ( o + t * d - vr.ui_position );
                const float u = q.x / ui_size.x + 0.5f;
                const float v = q.y / ui_size.y + 0.5f;
                if ( u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f ) {
                    // Top-left origin, y down — same as SDL mouse.
                    globalGameState.input.mouse.absPosition.x = static_cast<int>( u * globalGameState.screen.width );
                    globalGameState.input.mouse.absPosition.y = static_cast<int>( ( 1.0f - v ) * globalGameState.screen.height );
                    vr.pointer_on_ui = true;
                }
            }
        }
    }
}

// Folds the tracked head pose into the camera angles so block raycasts,
// movement direction and remote-avatar facing all follow where the head
// actually points. body_yaw (mouse) supplies the yaw base.
void RepGame::apply_vr_head_angles( ) {
    VrState &vr = globalGameState.vr;
    const glm::vec3 f = vr.head * glm::vec3( 0.0f, 0.0f, -1.0f );
    const float head_yaw = glm::degrees( atan2f( f.x, -f.z ) );
    const float head_pitch = glm::degrees( -asinf( glm::clamp( f.y, -1.0f, 1.0f ) ) );
    globalGameState.camera.angle_H = wrap_degrees_360( vr.body_yaw + head_yaw );
    globalGameState.camera.angle_V = head_pitch;
}

// Applies this frame's synced controller state onto the shared Input fields.
// Everything is edge-detected against vr.prev_input so real keyboard/mouse
// input keeps working in parallel.
void RepGame::process_vr_input( ) {
    Input &input = globalGameState.input;
    const VrControllerInput &in = globalGameState.vr.input;
    const VrControllerInput &prev = globalGameState.vr.prev_input;

    // While the pause menu is up no controller action reaches gameplay.
    // prev still tracks in so nothing edge-fires when the menu closes.
    if ( input.menu_open ) {
        input.movement.sizeH = 0.0f;
        input.movement.angleH = 0.0f;
        input.movement.jumpPressed = false;
        input.movement.sneakPressed = false;
        globalGameState.vr.prev_input = globalGameState.vr.input;
        return;
    }

    // Left stick drives horizontal movement. Inside the deadzone the stick
    // hands control back to the keyboard exactly once (move_active).
    const float move_len = glm::length( in.move );
    if ( move_len > 0.2f ) {
        input.movement.sizeH = 1.0f;
        input.movement.angleH = glm::degrees( atan2f( in.move.x, in.move.y ) );
        globalGameState.vr.move_active = true;
    } else if ( globalGameState.vr.move_active ) {
        input.movement.sizeH = 0.0f;
        input.movement.angleH = 0.0f;
        globalGameState.vr.move_active = false;
    }

    if ( in.jump != prev.jump ) {
        input.movement.jumpPressed = in.jump;
    }
    if ( in.sneak != prev.sneak ) {
        input.movement.sneakPressed = in.sneak;
    }
    if ( in.sprint != prev.sprint ) {
        input.player_sprinting = in.sprint;
    }
    if ( in.toggle_fly && !prev.toggle_fly ) {
        input.player_flying = !input.player_flying;
    }
    if ( in.inventory && !prev.inventory ) {
        // Same toggle as the 'e' key.
        input.inventory_open = !input.inventory_open;
    }

    // wheel_diff = previousPosition.wheel_counts - currentPosition.wheel_counts,
    // and scrolling the wheel DOWN decrements wheel_counts — so "next" moves
    // the selection the way one wheel-down notch does.
    if ( in.hotbar_next && !prev.hotbar_next ) {
        input.mouse.currentPosition.wheel_counts -= 1;
    }
    if ( in.hotbar_prev && !prev.hotbar_prev ) {
        input.mouse.currentPosition.wheel_counts += 1;
    }

    // Mouse buttons mirror Input::mouseInput: press edge marks the click
    // unhandled, release edge marks it handled.
    if ( in.break_block != prev.break_block ) {
        input.mouse.buttons.left = in.break_block;
        input.mouse.buttons.left_click_handled = !in.break_block;
        input.mouse.buttons.left_pressed_in_ui = in.break_block && input.inventory_open;
    }
    if ( in.place_block != prev.place_block ) {
        input.mouse.buttons.right = in.place_block;
        input.mouse.buttons.right_click_handled = !in.place_block;
        input.mouse.buttons.right_pressed_in_ui = in.place_block && input.inventory_open;
    }
    if ( in.pick_block != prev.pick_block ) {
        input.mouse.buttons.middle = in.pick_block;
        input.mouse.buttons.middle_click_handled = !in.pick_block;
        input.mouse.buttons.middle_pressed_in_ui = in.pick_block && input.inventory_open;
    }

    // Right stick turns the body. Snap by default (with a re-arm deadband);
    // REPGAME_VR_SMOOTH_TURN (deg/sec, default 0 = off) switches to smooth.
    static const float smooth_turn_rate = env_float( "REPGAME_VR_SMOOTH_TURN", 0.0f );
    static const float snap_degrees = env_float( "REPGAME_VR_SNAP_DEGREES", 30.0f );
    if ( smooth_turn_rate > 0.0f ) {
        if ( fabsf( in.turn.x ) > 0.2f ) {
            globalGameState.vr.body_yaw += in.turn.x * smooth_turn_rate / static_cast<float>( UPS_RATE );
        }
    } else {
        const float ax = fabsf( in.turn.x );
        if ( globalGameState.vr.turn_armed && ax > 0.7f ) {
            // Positive x turns right: angle_H increases.
            globalGameState.vr.body_yaw += in.turn.x > 0.0f ? snap_degrees : -snap_degrees;
            globalGameState.vr.turn_armed = false;
        } else if ( ax < 0.3f ) {
            globalGameState.vr.turn_armed = true;
        }
    }
    globalGameState.vr.body_yaw = wrap_degrees_360( globalGameState.vr.body_yaw );

    globalGameState.vr.prev_input = globalGameState.vr.input;
}
void RepGame::draw_vr_ui( unsigned int fbo, int width, int height ) {
    // Save/restore the clear color so the transparent clear doesn't leak
    // into the flat or eye paths.
    float prev_clear[ 4 ];
    glGetFloatv( GL_COLOR_CLEAR_VALUE, prev_clear );
    FrameBuffer::set_display( fbo );
    FrameBuffer::bind_display( );
    glViewport( 0, 0, width, height );
    glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
    glClearColor( prev_clear[ 0 ], prev_clear[ 1 ], prev_clear[ 2 ], prev_clear[ 3 ] );

    // The compositor blends the quad with the scene, so the layer's alpha
    // must be premultiplied: color keeps alpha-weighted rgb (SRC_ALPHA),
    // alpha itself accumulates coverage (ONE, ONE_MINUS_SRC_ALPHA).
    glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
    const glm::dvec3 render_pos = globalGameState.camera.pos + glm::dvec3( 0.0, globalGameState.camera.eye_height_render, 0.0 );
    const float daylight = glm::max( light_daylight_factor( globalGameState.world_time ), imgui_overlay_get_imgui_debug_vars( ).night_ambient );
    draw_ui_pass( render_pos, daylight, false );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    FrameBuffer::set_display( 0 );
}

VrUiLayer RepGame::get_vr_ui_layer( ) const {
    const glm::vec2 size = repgame_vr_ui_size( globalGameState.screen.width, globalGameState.screen.height );
    VrUiLayer layer;
    layer.size = size;
    if ( globalGameState.vr.ui_world_locked ) {
        layer.world_locked = true;
        layer.orientation = globalGameState.vr.ui_orientation;
        layer.position = globalGameState.vr.ui_position;
    } else {
        layer.world_locked = false;
        layer.orientation = glm::quat( 1.0f, 0.0f, 0.0f, 0.0f );
        layer.position = glm::vec3( 0.0f, 0.0f, -repgame_vr_ui_dist( ) );
    }
    return layer;
}
