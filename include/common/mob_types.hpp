#pragma once

#include "common/mob_models_gen.hpp"
#include "common/item.hpp"

// One roll of a mob's drop table: `min`..`max` of `item` (uniform).
typedef struct {
    ItemID item;
    int min;
    int max;
} MobDrop;

enum MobType { MOB_PIG = 0, MOB_PLAYER, MOB_COW, MOB_CHICKEN, MOB_SHEEP, MOB_ZOMBIE, MOB_CREEPER, MOB_BEE, MOB_TYPE_COUNT };

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
    // Fly height above the local ground in blocks; 0 = grounded mob. The
    // bee hovers, climbing over ledges and trees instead of jumping.
    float hover_h;
    // Items handed to the player when the mob is killed in survival. Unused
    // entries have item == LAST_BLOCK_ID.
    MobDrop drops[ 2 ];
    // Item that hatches this mob on right-click (the spawn eggs in
    // data/items); LAST_BLOCK_ID = no egg (the remote player type).
    ItemID spawn_egg;
    // Breeding (passive types): food items that tempt the mob toward the
    // player and put it in love mode when fed; LAST_BLOCK_ID-terminated.
    // An empty list means the type can't breed (player, hostiles). The
    // first entry is also the `feed` debug command's default food.
    ItemID breed_items[ 4 ];
    // Bee only: every `is_flower` block (the "flowers" item tag) counts as
    // breed food on top of breed_items — the flower set doesn't fit the
    // fixed list.
    bool breeds_with_flowers;
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

// The mob a spawn-egg item hatches (MobTypeDef::spawn_egg), or
// MOB_TYPE_COUNT for non-egg items — including LAST_BLOCK_ID, the
// remote-player type's no-egg sentinel (and the empty hand).
inline MobType mob_type_for_spawn_egg( ItemID item ) {
    if ( item == LAST_BLOCK_ID ) {
        return MOB_TYPE_COUNT;
    }
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        if ( mob_types[ t ].spawn_egg == item ) {
            return static_cast<MobType>( t );
        }
    }
    return MOB_TYPE_COUNT;
}

// Whether `item` is one of this type's breed foods (MobTypeDef::breed_items
// plus, for breeds_with_flowers types, any is_flower block).
inline bool mob_def_is_breed_item( const MobTypeDef &def, ItemID item ) {
    if ( item == LAST_BLOCK_ID ) {
        return false;
    }
    for ( int i = 0; i < 4 && def.breed_items[ i ] != LAST_BLOCK_ID; i++ ) {
        if ( def.breed_items[ i ] == item ) {
            return true;
        }
    }
    return def.breeds_with_flowers && item_is_block( item ) && //
           block_definition_get_definition( static_cast<BlockID>( item ) )->is_flower;
}
