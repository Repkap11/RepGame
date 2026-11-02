#pragma once

// VR-facing types: the platform fills VrControllerInput each frame and
// reads VrUiLayer for the compositor; RenderView carries one eye's draw
// target (flat rendering passes views==nullptr to draw_frame). VrState
// holds the session state inside RepGameState.

#define GLM_FORCE_RADIANS
#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include <gtc/type_ptr.hpp>
#include <gtc/matrix_transform.hpp>

#include "common/world.hpp" // WorldDrawQuality

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

// VR state (OpenXR session). active means stereo rendering is in use:
// mouse dx then turns the body (body_yaw) and the HMD's local orientation
// supplies head — camera.angle_H/V are recomputed as body+head each tick
// so raycasts, movement and remote avatars track the real head.
struct VrState {
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
};
