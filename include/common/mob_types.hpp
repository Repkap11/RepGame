#pragma once

#include "common/mob_models_gen.hpp"
#include "common/block_definitions.hpp"

enum MobType { MOB_PIG = 0, MOB_PLAYER, MOB_COW, MOB_CHICKEN, MOB_SHEEP, MOB_ZOMBIE, MOB_CREEPER, MOB_TYPE_COUNT };

// AI profiles — the extension point for future hostile or flying mobs.
enum MobAI {
    MOB_AI_PASSIVE_WANDER = 0,
    MOB_AI_REMOTE, // pose driven by the network (remote players)
    MOB_AI_HOSTILE_MELEE,   // chase + melee the player (zombie)
    MOB_AI_HOSTILE_CREEPER, // chase + fuse explosion (creeper)
};

// Procedural animators; MobTypeDef::anims is a bitmask of these.
enum MobAnim {
    MOB_ANIM_QUADRUPED_WALK = 1 << 0, // leg0/3 vs leg1/2 diagonal gait
    MOB_ANIM_HEAD_LOOK = 1 << 1,      // head tracks the nearest player
    MOB_ANIM_DEATH_TIP = 1 << 2,      // 90° side roll over 0.5 s (all types)
    MOB_ANIM_HUMANOID_WALK = 1 << 3,  // opposite arm+leg swing (Bedrock humanoid.move)
    MOB_ANIM_HUMANOID_SNEAK = 1 << 4, // crouch pose (Java ModelBiped-style offsets)
    MOB_ANIM_WING_FLAP = 1 << 5,      // wings flap on z, faster while airborne
    MOB_ANIM_SWELL = 1 << 6,          // creeper fuse swelling + white flash
};

typedef struct {
    const char *name; // REPGAME_MOB_TEST type name
    MobModel model;
    MobSkin skins[ 4 ];
    int skin_count;
    // Collision AABB: full width/height in blocks; eye_h is the feet-to-eye
    // height used for look-at and light sampling.
    float hitbox_w;
    float hitbox_h;
    float eye_h;
    float max_health;
    // Horizontal speed in blocks/tick.
    float walk_speed;
    float panic_speed;
    // Hostile AIs: speed while chasing and melee damage per hit (0 for
    // passive types).
    float chase_speed;
    float attack_damage;
    // Max downward speed in blocks/tick; 0 = no cap (chicken glides).
    float fall_cap;
    BlockID drop_block;
    int drop_min;
    int drop_max;
    // Sound pool names; NULL when the type has no such sound. Consumed by the
    // audio pass (phase 2).
    const char *sound_say;
    const char *sound_hurt;
    const char *sound_death;
    const char *sound_step;
    // Base volume of sound_step before distance falloff (Java plays mob
    // steps quietly, e.g. 0.15 for pigs).
    float step_gain;
    // Natural spawning (phase 2): spawn_chance per eligible fresh chunk, on
    // top of spawn_on blocks, in herds of herd_min..herd_max.
    BlockID spawn_on;
    float spawn_chance;
    int herd_min;
    int herd_max;
    MobAI ai;
    unsigned int anims;
    // Bone names the animators bind to (head for look-at, legs for the gait).
    // Quadrupeds use bone_legs[0..3]; humanoids use bone_legs[0..1] and
    // bone_arms[0..1], each as (right, left).
    const char *bone_head;
    const char *bone_legs[ 4 ];
    const char *bone_arms[ 2 ];
    const char *bone_body;       // humanoid torso for the sneak lean; NULL elsewhere
    const char *bone_wings[ 2 ]; // chicken wing bones (wing0 +z flap, wing1 -z)
} MobTypeDef;

extern const MobTypeDef mob_types[ MOB_TYPE_COUNT ];
