#pragma once

// Local-player pieces of RepGameState: the camera (pose + physics) and the
// crosshair interaction state (block selection, survival mining).

#include <glm.hpp>

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
    glm::dvec3 prev_pos;
    double prev_eye_height = PLAYER_EYE_HEIGHT;
    float prev_angle_H;
    float prev_angle_V;
};

// The block/mob under the crosshair this tick: block cell selection plus
// the mob raycast winner when it is nearer than the block cell.
struct BlockSelection {
    int selectionInBounds;
    int face;
    glm::ivec3 pos_create;
    glm::ivec3 pos_destroy;
    // Second raycast that can hit water — only run while holding a
    // can_float_on_water block, and only used to place it.
    int floatSelectionInBounds;
    glm::ivec3 pos_create_water;
    // The camera/aim ray this selection came from — set where the ray is
    // computed so left-click can test mob hits against the same ray.
    glm::dvec3 ray_origin;
    glm::dvec3 ray_dir;
    // The mob under the crosshair: the mob raycast winner when its hitbox
    // entry is nearer than the selected block cell's, else -1. When set,
    // selectionInBounds/floatSelectionInBounds are cleared so no block
    // outline (or place/pick) happens through the mob.
    int target_mob;
    double target_mob_dist;
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
