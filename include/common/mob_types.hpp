#pragma once

#include "common/mob_models_gen.hpp"
#include "common/block_definitions.hpp"

enum MobType { MOB_PIG = 0, MOB_TYPE_COUNT };

// AI profiles — the extension point for future hostile or flying mobs.
enum MobAI { MOB_AI_PASSIVE_WANDER = 0 };

// Procedural animators; MobTypeDef::anims is a bitmask of these.
enum MobAnim {
    MOB_ANIM_QUADRUPED_WALK = 1 << 0, // leg0/3 vs leg1/2 diagonal gait
    MOB_ANIM_HEAD_LOOK = 1 << 1,      // head tracks the nearest player
    MOB_ANIM_DEATH_TIP = 1 << 2,      // 90° side roll over 0.5 s (all types)
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
    BlockID drop_block;
    int drop_min;
    int drop_max;
    // Sound pool names; NULL when the type has no such sound. Consumed by the
    // audio pass (phase 2).
    const char *sound_say;
    const char *sound_hurt;
    const char *sound_death;
    const char *sound_step;
    // Natural spawning (phase 2): spawn_chance per eligible fresh chunk, on
    // top of spawn_on blocks, in herds of herd_min..herd_max.
    BlockID spawn_on;
    float spawn_chance;
    int herd_min;
    int herd_max;
    MobAI ai;
    unsigned int anims;
    // Bone names the animators bind to (head for look-at, legs for the gait).
    const char *bone_head;
    const char *bone_legs[ 4 ];
} MobTypeDef;

extern const MobTypeDef mob_types[ MOB_TYPE_COUNT ];
