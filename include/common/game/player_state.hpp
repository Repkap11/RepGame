#pragma once

// Local-player pieces of RepGameState: the camera (pose + physics) and the
// crosshair interaction state (block selection, survival mining).

#include <glm/glm.hpp>

#include "common/constants.hpp"          // PLAYER_HEIGHT / PLAYER_EYE_HEIGHT
#include "common/block_definitions.hpp" // BlockID

// Camera pose + physics of the local player. World-space position/velocity
// is double precision so movement and collision stay correct at large
// coordinates; the prev_* snapshot interpolates the rendered camera between
// the previous and current tick.
struct CameraState {
    float angle_H;
    float angle_V;
    glm::vec3 look;
    glm::mat4 rotation;
    glm::vec3 movement;
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
    // Riding a minecart: the cart id (0 = not riding). While set,
    // process_movement skips all body physics — the seat position comes
    // from the cart's follow pass and rider_push carries the W/S intent
    // fed to the cart tick.
    uint32_t riding_cart = 0;
    glm::dvec2 rider_push;
    // Sneak dismount edge: set once sneak has been released after
    // mounting, so a sneak held through the mount click doesn't eject.
    int ride_sneak_seen_up = 0;
    glm::dvec3 prev_pos;
    double prev_eye_height = PLAYER_EYE_HEIGHT;
    float prev_angle_H;
    float prev_angle_V;
    // Copies the current transform into the prev_* interpolation snapshot —
    // once per tick, and wherever a pin/teleport must not blend across.
    void snapshot_prev( ) {
        prev_pos = pos;
        prev_eye_height = eye_height;
        prev_angle_H = angle_H;
        prev_angle_V = angle_V;
    }
};

// Local-player health and breath. `health` is in half-hearts
// (0..PLAYER_MAX_HEALTH) — the HUD shows 10 heart slots, so 2 hp per
// heart. `air_ticks` is the breath meter (bubbles on the HUD while below
// max); `hurt_cooldown` is the post-hit invulnerability window shared by
// every damage source; `dead` drives the death screen until respawn.
struct PlayerVitals {
    float health = PLAYER_MAX_HEALTH;
    int air_ticks = PLAYER_AIR_MAX_TICKS;
    int hurt_cooldown = 0;
    int regen_accum = 0;
    bool dead = false;
    // Real downward travel since last ground contact (shared by the
    // farmland trample and fall damage); flying, no-clip, climbing and
    // swimming don't count — water entry resets it.
    double fall_dist = 0.0;
};

// The block/mob under the crosshair this tick: block cell selection plus
// the mob raycast winner when it is nearer than the block's pick hit.
struct BlockSelection {
    int selectionInBounds;
    int face;
    glm::ivec3 pos_create;
    glm::ivec3 pos_destroy;
    // Y of the exact ray hit inside pos_destroy (0-1): drives stair
    // upside-down placement (upper half of a side face -> top-half stair).
    double hit_y_in_cell;
    // Distance along ray_dir where the pick ray entered the selected
    // block's pick shape (the find_block_from_to hit_t). The mob/cart
    // resolves compare hitbox entries against this — not the cell
    // boundary, so an entity sitting inside the selected cell (a cart on
    // its rail) still wins. REACH_DISTANCE when nothing is selected.
    double block_hit_dist;
    // Second raycast that can hit water — only run while holding a
    // can_float_on_water block, and only used to place it.
    int floatSelectionInBounds;
    glm::ivec3 pos_create_water;
    // Third raycast that hits any fluid — only run while holding a bucket:
    // pos_fluid is the hit fluid cell, pos_fluid_create the face-adjacent
    // cell a pour would target.
    int fluidSelectionInBounds;
    glm::ivec3 pos_fluid;
    glm::ivec3 pos_fluid_create;
    // The camera/aim ray this selection came from — set where the ray is
    // computed so left-click can test mob hits against the same ray.
    glm::dvec3 ray_origin;
    glm::dvec3 ray_dir;
    // The mob under the crosshair: the mob raycast winner when its hitbox
    // entry is nearer than block_hit_dist, else -1. When set,
    // selectionInBounds/floatSelectionInBounds are cleared so no block
    // outline (or place/pick) happens through the mob.
    int target_mob;
    double target_mob_dist;
    // Same for a minecart under the crosshair: the cart raycast winner
    // when nearer than the selected block (and than target_mob), else -1.
    int target_cart;
    double target_cart_dist;
};

// Survival-mode mining: while the left button is held on the same block,
// progress_ticks accumulates once per tick until it reaches the block's
// hardness * UPS_RATE, then the block breaks. Releasing the button,
// aiming at a different block, or opening the inventory resets it.
struct BlockMining {
    glm::ivec3 pos;
    BlockID id;
    float progress_ticks;
};

// First-person held-item swing: tick() advances `phase` once per tick —
// looping 0->1 while `swinging` (a left or right button held on the
// world: mine/attack or place/use) and, once released, finishing the
// in-flight chop out to 1 (back at the rest pose) where it parks at 0.
// `prev_phase` is the per-tick snapshot the renderer interpolates from,
// like the camera's prev_ fields.
struct HeldSwing {
    float phase = 0.0f;
    float prev_phase = 0.0f;
    // Ticks per chop — ~0.3s at UPS_RATE.
    static constexpr float PERIOD_TICKS = UPS_RATE * 0.3f;
    void tick( bool swinging ) {
        prev_phase = phase;
        if ( swinging ) {
            phase += 1.0f / PERIOD_TICKS;
            if ( phase >= 1.0f ) {
                phase -= 1.0f;
            }
        } else if ( phase > 0.0f ) {
            phase += 1.0f / PERIOD_TICKS;
            if ( phase >= 1.0f ) {
                phase = 0.0f;
            }
        }
    }
};
