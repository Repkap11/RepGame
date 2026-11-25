#include "common/mobs.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <stdlib.h>
#include <unordered_map>

#include "common/RepGame.hpp"
#include "common/arrows.hpp"
#include "common/audio.hpp"
#include "common/chunk.hpp"
#include "common/heart_particles.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/dimension_profile.hpp"
#include "common/imgui_overlay.hpp"
#include "common/item_entities.hpp"
#include "common/light.hpp"
#include "common/mp_test.hpp"
#include "common/net_entities.hpp"
#include "common/ui_overlay_buffers.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/angles.hpp"
#include "common/world.hpp"

#include "mob_internal.hpp"

// Bone rest + animator rotation, in degrees, plus an animator position
// offset in model pixels. The offset translates the whole bone (pivot and
// children) in the parent frame.
glm::mat4 mob_bone_local_matrix( const MobBoneDef &bone, const glm::vec3 &anim_rot, const glm::vec3 &anim_offset ) {
    const glm::vec3 pivot( bone.pivot[ 0 ], bone.pivot[ 1 ], bone.pivot[ 2 ] );
    const glm::vec3 rot = glm::radians( glm::vec3( bone.rest_rot[ 0 ] + anim_rot.x, bone.rest_rot[ 1 ] + anim_rot.y, bone.rest_rot[ 2 ] + anim_rot.z ) );
    // Same composition as translate(pivot)·rotate(x)·rotate(y)·rotate(z)·translate(-pivot)
    // pre-multiplied by translate(anim_offset): R = Rx·Ry·Rz built straight
    // from sin/cos, with the pivot re-centering folded into the translation.
    const float cx = cosf( rot.x ), sx = sinf( rot.x );
    const float cy = cosf( rot.y ), sy = sinf( rot.y );
    const float cz = cosf( rot.z ), sz = sinf( rot.z );
    glm::mat4 m;
    m[ 0 ] = glm::vec4( cy * cz, sx * sy * cz + cx * sz, -cx * sy * cz + sx * sz, 0.0f );
    m[ 1 ] = glm::vec4( -cy * sz, -sx * sy * sz + cx * cz, cx * sy * sz + sx * cz, 0.0f );
    m[ 2 ] = glm::vec4( sy, -sx * cy, cx * cy, 0.0f );
    const glm::vec3 rp = glm::vec3( m[ 0 ] ) * pivot.x + glm::vec3( m[ 1 ] ) * pivot.y + glm::vec3( m[ 2 ] ) * pivot.z;
    m[ 3 ] = glm::vec4( pivot + anim_offset - rp, 1.0f );
    return m;
}

void mob_bone_worlds( const MobModelDef &model, const glm::vec3 *anim_rot, const glm::vec3 *anim_offset, glm::mat4 *out ) {
    const int bone_count = model.bone_count < 32 ? model.bone_count : 32;
    const glm::vec3 zero( 0.0f );
    for ( int i = 0; i < bone_count; i++ ) {
        const MobBoneDef &bone = model.bones[ i ];
        const glm::mat4 local = mob_bone_local_matrix( bone, anim_rot ? anim_rot[ i ] : zero, anim_offset ? anim_offset[ i ] : zero );
        out[ i ] = bone.parent >= 0 ? out[ bone.parent ] * local : local;
    }
}

glm::mat4 mob_cube_matrix( const MobBoneDef &bone, const MobCubeDef &cube ) {
    const glm::vec3 origin( cube.origin[ 0 ] - cube.inflate, cube.origin[ 1 ] - cube.inflate, cube.origin[ 2 ] - cube.inflate );
    const glm::vec3 size( cube.size[ 0 ] + 2.0f * cube.inflate, cube.size[ 1 ] + 2.0f * cube.inflate, cube.size[ 2 ] + 2.0f * cube.inflate );
    glm::mat4 cube_m = glm::scale( glm::translate( glm::mat4( 1.0f ), origin ), size );
    // A cube rot (MobCubeDef::rot — cubes authored pre-rotated, like the
    // cat's vertical body) spins the textured box about the bone pivot.
    // Inside the bone transform so the bone's children don't move; on the
    // verts so each face keeps its painted texels — remapping origin/size
    // to an AABB would scramble the box-uv face layout.
    if ( cube.rot[ 0 ] != 0.0f || cube.rot[ 1 ] != 0.0f || cube.rot[ 2 ] != 0.0f ) {
        const glm::vec3 pivot( bone.pivot[ 0 ], bone.pivot[ 1 ], bone.pivot[ 2 ] );
        glm::mat4 rot_m = glm::translate( glm::mat4( 1.0f ), pivot );
        rot_m = glm::rotate( rot_m, glm::radians( cube.rot[ 0 ] ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
        rot_m = glm::rotate( rot_m, glm::radians( cube.rot[ 1 ] ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
        rot_m = glm::rotate( rot_m, glm::radians( cube.rot[ 2 ] ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
        rot_m = glm::translate( rot_m, -pivot );
        cube_m = rot_m * cube_m;
    }
    return cube_m;
}

namespace {

// Resolved once per (MobType, model): bone_worlds' name probes and
// update_render's arm-bone / cube-matrix lookups become table reads.
MobTypeBindings mob_build_bindings( const MobTypeDef &def, const MobModelDef &model ) {
    MobTypeBindings out;
    const int bone_count = model.bone_count < 32 ? model.bone_count : 32;
    for ( int i = 0; i < bone_count; i++ ) {
        const char *name = model.bones[ i ].name;
        MobBoneBinding &b = out.bones[ i ];
        b.head = def.bone_head != NULL && strcmp( name, def.bone_head ) == 0;
        b.body = def.bone_body != NULL && strcmp( name, def.bone_body ) == 0;
        for ( int k = 0; k < 4; k++ ) {
            if ( def.bone_legs[ k ] != NULL && strcmp( name, def.bone_legs[ k ] ) == 0 ) {
                b.leg = k;
            }
        }
        for ( int k = 0; k < 2; k++ ) {
            if ( def.bone_arms[ k ] != NULL && strcmp( name, def.bone_arms[ k ] ) == 0 ) {
                b.arm = k;
            }
            if ( def.bone_wings[ k ] != NULL && strcmp( name, def.bone_wings[ k ] ) == 0 ) {
                b.wing = k;
            }
        }
        if ( strncmp( name, "tentacles_", 10 ) == 0 ) {
            b.tentacle = atoi( name + 10 );
        }
        if ( strncmp( name, "upperBodyParts", 14 ) == 0 ) {
            b.rod = atoi( name + 14 );
        }
        if ( out.held_arm_bone < 0 && def.bone_arms[ 1 ] != NULL && strcmp( name, def.bone_arms[ 1 ] ) == 0 ) {
            out.held_arm_bone = i;
        }
    }
    return out;
}

// Slot 0 = MobTypeDef::model, slot 1 = sheared_model (base model again when
// the type has none — that slot is then never selected anyway).
std::array<std::array<MobTypeBindings, 2>, MOB_TYPE_COUNT> mob_build_binding_table( ) {
    std::array<std::array<MobTypeBindings, 2>, MOB_TYPE_COUNT> table;
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        const MobTypeDef &def = mob_types[ t ];
        table[ t ][ 0 ] = mob_build_bindings( def, mob_models[ def.model ] );
        table[ t ][ 1 ] = mob_build_bindings( def, mob_models[ def.sheared_model != MOB_MODEL_COUNT ? def.sheared_model : def.model ] );
    }
    return table;
}

// mob_cube_matrix over every model's (bone, cube) pairs — static generated
// data keyed by model index, so per-frame calls become lookups.
std::array<std::vector<std::vector<glm::mat4>>, MOB_MODEL_COUNT> mob_build_cube_mats( ) {
    std::array<std::vector<std::vector<glm::mat4>>, MOB_MODEL_COUNT> cache;
    for ( int m = 0; m < MOB_MODEL_COUNT; m++ ) {
        const MobModelDef &model = mob_models[ m ];
        const int bone_count = model.bone_count < 32 ? model.bone_count : 32;
        cache[ m ].resize( bone_count );
        for ( int b = 0; b < bone_count; b++ ) {
            const MobBoneDef &bone = model.bones[ b ];
            cache[ m ][ b ].reserve( bone.cube_count );
            for ( int c = 0; c < bone.cube_count; c++ ) {
                cache[ m ][ b ].push_back( mob_cube_matrix( bone, model.cubes[ bone.cube_start + c ] ) );
            }
        }
    }
    return cache;
}

} // namespace

const MobTypeBindings &mob_bone_bindings( MobType type, bool sheared ) {
    static const std::array<std::array<MobTypeBindings, 2>, MOB_TYPE_COUNT> table = mob_build_binding_table( );
    return table[ type ][ sheared ? 1 : 0 ];
}

static const std::vector<std::vector<glm::mat4>> &mob_cube_mats( int model ) {
    static const std::array<std::vector<std::vector<glm::mat4>>, MOB_MODEL_COUNT> cache = mob_build_cube_mats( );
    return cache[ model ];
}

void MobManager::bone_worlds( const Mob &mob, const MobTypeDef &def, float alpha, glm::mat4 *out ) {
    // The sheared model shares the woolly one's bone set, so the same anim
    // names resolve — pick whichever the mob is currently wearing.
    const MobModelDef &model =
        mob.sheared && def.sheared_model != MOB_MODEL_COUNT ? mob_models[ def.sheared_model ] : mob_models[ def.model ];
    const int bone_count = model.bone_count < 32 ? model.bone_count : 32;

    // Per-bone animator rotations and position offsets (model pixels),
    // resolved by name from the type table.
    glm::vec3 bone_anim[ 32 ];
    glm::vec3 bone_offset[ 32 ];
    for ( int i = 0; i < bone_count; i++ ) {
        bone_anim[ i ] = glm::vec3( 0.0f );
        bone_offset[ i ] = glm::vec3( 0.0f );
    }
    const float phase = glm::radians( mob.walk_dist * MOB_GAIT_UNITS_PER_BLOCK * 38.17f );
    const MobTypeBindings &bind = mob_bone_bindings( mob.type, mob.sheared != 0 );
    for ( int i = 0; i < bone_count; i++ ) {
        const MobBoneBinding &bb = bind.bones[ i ];
        if ( ( def.anims & MOB_ANIM_HEAD_LOOK ) && bb.head ) {
            bone_anim[ i ] = mob_head_anim( mob.head_yaw, mob.head_pitch );
        }
        if ( def.anims & MOB_ANIM_QUADRUPED_WALK ) {
            const float gait_deg = def.gait_scale > 0.0f ? 80.0f * def.gait_scale : 80.0f;
            if ( bb.leg >= 0 ) {
                const float sign = ( bb.leg == 0 || bb.leg == 3 ) ? 1.0f : -1.0f;
                bone_anim[ i ].x += sign * gait_deg * cosf( phase ) * mob.leg_amp;
            }
        }
        if ( def.anims & MOB_ANIM_WING_FLAP ) {
            // Bedrock animation.chicken.general: wing0 flaps +z, wing1 -z.
            // 20° on the ground, 70° while airborne (gliding).
            const float amp = mob.standing ? 20.0f : 70.0f;
            if ( bb.wing >= 0 ) {
                bone_anim[ i ].z += ( bb.wing == 0 ? 1.0f : -1.0f ) * amp * sinf( mob.wing_phase );
            }
        }
        if ( def.anims & MOB_ANIM_GHAST_TENTACLES ) {
            // Bedrock animation.ghast.move: each tentacles_N bone sways
            // ±11.5° on x around its -23° rest splay (baked into the geo —
            // negative tips the tentacles back so they drag behind),
            // phased ~1 rad apart per tentacle at a ~1 Hz rate — the slow
            // wing_phase reuse keeps the drift gentle.
            if ( bb.tentacle >= 0 ) {
                bone_anim[ i ].x += 11.5f * sinf( mob.wing_phase * 0.11f + static_cast<float>( bb.tentacle ) );
            }
        }
        if ( def.anims & MOB_ANIM_BLAZE_RODS ) {
            // Bedrock animation.blaze.move: the 12 upperBodyPartsN bones
            // share one cube at the centre and the position channel rides
            // each on a ring around the head — outer r=9 (rods 0-3) at
            // -360°/s, middle r=7 (4-7) at +108°/s, inner r=5 (8-11) at
            // -180°/s — with a fixed y offset (+2, -2, -11 px) and a ±1 px
            // per-rod bob. anim_phase is a constant-rate clock: the orbit
            // speeds are fixed, so wing_phase's state-varying rate won't do.
            if ( bb.rod >= 0 ) {
                const int rod = bb.rod;
                const int ring = rod / 4;
                static const float ring_r[ 3 ] = { 9.0f, 7.0f, 5.0f };
                static const float ring_w[ 3 ] = { -360.0f, 108.0f, -180.0f }; // deg/s
                static const float ring_p[ 3 ] = { 0.0f, 45.0f, 27.0f };       // deg
                static const float ring_y[ 3 ] = { 2.0f, -2.0f, -11.0f };      // px
                const float t_s = mob.anim_phase / UPS_RATE;
                const float a = glm::radians( ring_p[ ring ] + ( rod % 4 ) * 90.0f + ring_w[ ring ] * t_s );
                bone_offset[ i ].x = ring_r[ ring ] * cosf( a );
                bone_offset[ i ].z = ring_r[ ring ] * sinf( a );
                const float bob_phase = rod * ( ring == 2 ? 21.48f : 28.64f ); // deg
                bone_offset[ i ].y = ring_y[ ring ] + cosf( glm::radians( 286.4f * t_s + bob_phase ) );
            }
        }
        if ( def.anims & MOB_ANIM_HUMANOID_WALK ) {
            // Bedrock animation.humanoid.move: tcos0 = cos(phase)*57.3°,
            // opposite arm and leg swing together (right leg + left arm).
            const float tcos0 = cosf( phase ) * 57.3f * mob.leg_amp;
            if ( bb.leg == 0 || bb.leg == 1 ) {
                bone_anim[ i ].x += ( bb.leg == 0 ? 1.4f : -1.4f ) * tcos0;
                // Seated in a minecart: thighs folded forward at the hip.
                if ( mob.seated ) {
                    bone_anim[ i ].x += 90.0f;
                }
            }
            if ( bb.arm >= 0 ) {
                bone_anim[ i ].x += ( bb.arm == 0 ? -1.0f : 1.0f ) * tcos0;
            }
        }
    }

    // The sneak pose is a model-space pre-multiply applied per named part
    // (S below), NOT folded into the bone-local matrices. In the Bedrock
    // humanoid rig head/hat and the arms are children of body, so a local
    // body lean would tilt the head and drag the arms; Java's ModelBiped
    // crouch values instead treat the parts as independent. S rotates a
    // part about its own model-space pivot and translates it; overlays and
    // other unnamed children inherit S from their nearest named ancestor,
    // while head/arms keep only their own S and skip body's lean entirely.
    // Walk swing and head look below stay in the bone-local matrices, so
    // they compose on the same pivot/axis.
    const float s = mob.prev_sneak + ( mob.sneak - mob.prev_sneak ) * alpha;

    for ( int i = 0; i < bone_count; i++ ) {
        const MobBoneDef &bone = model.bones[ i ];
        const glm::mat4 local = mob_bone_local_matrix( bone, bone_anim[ i ], bone_offset[ i ] );
        out[ i ] = bone.parent >= 0 ? out[ bone.parent ] * local : local;
    }

    if ( ( def.anims & MOB_ANIM_HUMANOID_SNEAK ) && s > 0.0f ) {
        glm::mat4 s_eff[ 32 ];
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            const MobBoneBinding &bb = bind.bones[ i ];
            float angle = 0.0f;
            glm::vec3 off( 0.0f );
            int named = 0;
            if ( bb.body ) {
                angle = -28.65f;
                off.y = -1.5f;
                named = 1;
            } else if ( bb.head ) {
                off.y = -2.5f;
                named = 1;
            } else {
                if ( bb.arm >= 0 ) {
                    angle = -22.9f;
                    off.y = -1.5f;
                    named = 1;
                }
                if ( bb.leg == 0 || bb.leg == 1 ) {
                    off.z = 4.0f;
                    named = 1;
                }
            }
            if ( named ) {
                const glm::vec3 pivot( bone.pivot[ 0 ], bone.pivot[ 1 ], bone.pivot[ 2 ] );
                glm::mat4 sm = glm::translate( glm::mat4( 1.0f ), pivot );
                sm = glm::rotate( sm, glm::radians( angle * s ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
                sm = glm::translate( sm, -pivot );
                s_eff[ i ] = glm::translate( glm::mat4( 1.0f ), off * s ) * sm;
            } else if ( bone.parent >= 0 ) {
                s_eff[ i ] = s_eff[ bone.parent ];
            } else {
                s_eff[ i ] = glm::mat4( 1.0f );
            }
            out[ i ] = s_eff[ i ] * out[ i ];
        }
    }
}

void MobManager::update_render( float alpha, const ChunkLoader &chunk_loader, HeartParticles *hearts, float time_s ) {
    PROFILE_SCOPE( "mobs_render" );
    if ( !this->render_inited ) {
        return;
    }
    for ( Mob &mob : this->mobs ) {
        const MobTypeDef &def = mob_types[ mob.type ];
        // A shorn shearable type renders its sheared model (a smaller cube
        // set sharing the bone names); spare parts allocated for the larger
        // model get a zeroed transform below.
        const int model_i = mob.sheared && def.sheared_model != MOB_MODEL_COUNT ? def.sheared_model : def.model;
        const MobModelDef &model = mob_models[ model_i ];

        const glm::dvec3 rpos = glm::mix( mob.prev_pos, mob.pos, static_cast<double>( alpha ) );
        const float ryaw = mob.prev_yaw + wrap_degrees_180( mob.yaw - mob.prev_yaw ) * alpha;

        // entity_T * yaw_R * scale(1/16): the model's -Z front lands on the
        // mob's forward direction (sin(yaw), 0, -cos(yaw)) — the same
        // convention as the player's angle_H. The unit cube's z=0 verts
        // (FACE_BACK tag) are the model's front; mob_face_rect maps that.
        glm::mat4 base = glm::translate( glm::mat4( 1.0f ), glm::vec3( rpos ) );
        base = glm::rotate( base, glm::radians( -ryaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
        if ( mob.death_ticks > 0 ) {
            // Tip-over: roll 90° onto the side over 0.5 s, pivoting at the
            // hitbox's mid height.
            const float tip_h = def.hitbox_h * 0.4f * scale_of( mob );
            const float roll = fminf( mob.death_ticks / ( 0.5f * UPS_RATE ), 1.0f ) * 90.0f;
            base = glm::translate( base, glm::vec3( 0.0f, tip_h, 0.0f ) );
            base = glm::rotate( base, glm::radians( roll ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
            base = glm::translate( base, glm::vec3( 0.0f, -tip_h, 0.0f ) );
        }
        // Creeper fuse swelling (Java's swelling math): a small high-frequency
        // jitter f' on top of the cubic ramp, squashing y while x/z inflate.
        float fuse_f = 0.0f;
        if ( ( def.anims & MOB_ANIM_SWELL ) && mob.fuse_ticks > 0 ) {
            const float raw = mob.fuse_ticks / ( 1.5f * UPS_RATE );
            const float fp = 1.0f + sinf( raw * 100.0f ) * raw * 0.01f;
            fuse_f = glm::clamp( raw, 0.0f, 1.0f );
            fuse_f = fuse_f * fuse_f * fuse_f;
            base = glm::scale( base, glm::vec3( ( 1.0f + fuse_f * 0.4f ) * fp, ( 1.0f + fuse_f * 0.1f ) / fp, ( 1.0f + fuse_f * 0.4f ) * fp ) );
        }
        base = glm::scale( base, glm::vec3( scale_of( mob ) * def.model_scale / 16.0f ) );

        // One light sample per mob covers all of its parts.
        const glm::vec2 light = chunk_loader.light_sample( glm::vec3( rpos ) + glm::vec3( 0.0f, def.eye_h * scale_of( mob ), 0.0f ) );

        // Breeding hearts: feeding opens a ~5 s window (heart_ticks —
        // mirrors get it from the LOVE flag edge) emitting one heart per
        // ~0.5 s; heart_burst is a one-shot group on top (newborn / fed
        // baby). Bounded, so emission can't run forever even though love
        // itself no longer expires. Capture mode stays clean — heart
        // emission would add run-to-run noise.
        if ( hearts != nullptr && mob.death_ticks == 0 && !RepGame::capture_mode( ) ) {
            float dt = time_s - mob.next_heart_at;
            // Half-wrap: next_heart_at can sit just past 3600 while time_s
            // has already wrapped to ~0 — only a diff past half the period
            // is a wrap, a sub-second negative diff is just "not due yet".
            if ( dt < -HEART_TIME_WRAP * 0.5f ) {
                dt += HEART_TIME_WRAP;
            }
            const glm::dvec3 above_head = rpos + glm::dvec3( 0.0, def.hitbox_h * scale_of( mob ) + 0.35, 0.0 );
            const bool emitting = mob.heart_ticks > 0;
            if ( emitting && ( !mob.was_emitting || dt >= 0.0f ) ) {
                hearts->spawn( above_head, light, 1, time_s );
                std::uniform_real_distribution<float> period( HEART_PERIOD_MIN, HEART_PERIOD_MIN + HEART_PERIOD_SPREAD );
                mob.next_heart_at = time_s + period( this->rng );
            }
            mob.was_emitting = emitting;
            if ( mob.heart_burst > 0 ) {
                hearts->spawn( above_head, light, mob.heart_burst, time_s );
                mob.heart_burst = 0;
            }
        }

        glm::mat4 bone_world[ 32 ];
        bone_worlds( mob, def, alpha, bone_world );

        int part = 0;
        const int bone_count = model.bone_count < 32 ? model.bone_count : 32;
        const std::vector<std::vector<glm::mat4>> &cube_mats = mob_cube_mats( model_i );
        const int held_arm = mob_bone_bindings( mob.type, mob.sheared != 0 ).held_arm_bone;
        glm::mat4 held_bone_m( 1.0f );
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            const glm::mat4 bone_m = base * bone_world[ i ];
            if ( i == held_arm ) {
                held_bone_m = bone_m;
            }
            for ( int c = 0; c < bone.cube_count; c++ ) {
                const MobCubeDef &cube = model.cubes[ bone.cube_start + c ];
                MobPartInstance &inst = this->render_chain.get_instance( mob.parts[ part++ ] );
                inst.transform = bone_m * cube_mats[ i ][ c ];
                inst.light = light;
                inst.uv_a = glm::vec4( cube.uv[ 0 ], cube.uv[ 1 ], cube.size[ 0 ], cube.size[ 1 ] );
                // uv_b.w < 0 is the red hurt flash; > 0 is the creeper's
                // white fuse flash on alternate tenths. Hurt wins.
                const bool white_flash = fuse_f > 0.0f && static_cast<int>( fuse_f * 10.0f ) % 2 == 0;
                // The ghast swaps to its open-mouth shooting skin while a
                // fireball charges (volley_ticks spans the windup) —
                // skins[1] is a state overlay, not a random variant.
                const MobSkin skin = def.ai == MOB_AI_HOSTILE_GHAST && def.skin_count > 1
                                         ? def.skins[ mob.volley_ticks > 0 ? 1 : 0 ]
                                         : def.skins[ mob.variant ];
                inst.uv_b = glm::vec4( cube.size[ 2 ], static_cast<float>( skin ),
                                       ( bone.mirror || cube.mirror ) ? 1.0f : 0.0f, mob.hurt_ticks > 0 ? -0.7f : white_flash ? 0.5f : 0.0f );
            }
        }
        // Parts allocated beyond the active model's cubes (the woolly model
        // is bigger than the sheared one) render nothing.
        while ( part < static_cast<int>( mob.parts.size( ) ) ) {
            MobPartInstance &inst = this->render_chain.get_instance( mob.parts[ part++ ] );
            inst.transform = glm::mat4( 0.0f );
        }

        // Held item: rebuild the instances when the streamed item changes,
        // then ride the right-arm bone so walk swing, the sneak pose and the
        // death tip-over all carry it (bone_world is in model pixels).
        if ( mob.held_drawn != mob.held_item ) {
            this->drop_held_parts( mob );
            mob.held_drawn = mob.held_item;
            if ( item_id_valid( mob.held_item ) ) {
                mob.held_cube = item_is_block( mob.held_item ) && item_icon_is_isometric( mob.held_item );
                if ( mob.held_cube ) {
                    const Block *block = block_definition_get_definition( static_cast<BlockID>( mob.held_item ) );
                    const int nboxes = block_icon_box_count( block );
                    unsigned short face[ NUM_FACES_IN_CUBE ];
                    resolve_item_faces( mob.held_item, face );
                    for ( int i = 0; i < nboxes; i++ ) {
                        const std::pair<entt::entity, BlockObjectInstance &> data = this->render_held_cube.create_instance( );
                        mob.held_parts.push_back( data.first );
                        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                            data.second.face[ f ] = face[ f ];
                        }
                        block_icon_box_faces( block, i, data.second.face );
                        block_item_shape( block, true, i, data.second );
                        data.second.transform = glm::mat4( 0.0f );
                    }
                } else {
                    const std::pair<entt::entity, ItemSpriteInstance &> data = this->render_held_sprite.create_instance( );
                    mob.held_parts.push_back( data.first );
                    data.second.tex_layer = item_icon_texture( mob.held_item );
                    data.second.transform = glm::mat4( 0.0f );
                }
            }
        }
        if ( !mob.held_parts.empty( ) && def.bone_arms[ 1 ] != NULL ) {
            if ( held_arm >= 0 ) {
                // The model's bone names are mirrored (front is -Z, so the
                // arm named "leftArm" at +X is the character's right hand —
                // matching MC). Grip pose in model pixels, tunable live in
                // the ImGui overlay's Avatar Held sections
                // (IMGUI_OVERLAY_LIVE_TUNE): item center near the fist's
                // bottom-front, icon face diagonal so it reads from the
                // character's front-right, top tipped forward.
                const ImGuiDebugVars &dv = imgui_overlay_get_imgui_debug_vars( );
                // The bow gets its own grip (like the first-person bow_*
                // override): avatar_bow_spin rotates the icon in-plane about
                // the sprite center so its diagonal reads as the limbs.
                const bool bow_held = !mob.held_cube && mob.held_item == BOW;
                const glm::vec3 &grip_pos = mob.held_cube ? dv.avatar_held_block_pos : ( bow_held ? dv.avatar_bow_pos : dv.avatar_held_pos );
                const glm::vec3 &grip_rot = mob.held_cube ? dv.avatar_held_block_rot : ( bow_held ? dv.avatar_bow_rot : dv.avatar_held_rot );
                const float grip_scale = mob.held_cube ? dv.avatar_held_block_scale : ( bow_held ? dv.avatar_bow_scale : dv.avatar_held_scale );
                glm::mat4 grip = glm::translate( glm::mat4( 1.0f ), grip_pos );
                grip = glm::rotate( grip, glm::radians( grip_rot.y ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
                grip = glm::rotate( grip, glm::radians( grip_rot.x ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
                grip = glm::rotate( grip, glm::radians( grip_rot.z ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
                grip = glm::scale( grip, glm::vec3( grip_scale ) );
                const glm::mat4 held_m = held_bone_m * grip;
                if ( mob.held_cube ) {
                    const glm::mat4 m = held_m * glm::translate( glm::mat4( 1.0f ), glm::vec3( -0.5f ) );
                    for ( entt::entity e : mob.held_parts ) {
                        BlockObjectInstance &inst = this->render_held_cube.get_instance( e );
                        inst.transform = m;
                        inst.light = light;
                    }
                } else {
                    glm::mat4 m = held_m;
                    if ( bow_held ) {
                        m = m * glm::rotate( glm::mat4( 1.0f ), glm::radians( dv.avatar_bow_spin ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
                    }
                    m = m * glm::translate( glm::mat4( 1.0f ), glm::vec3( -0.5f, -0.5f, -0.5f / 16.0f ) );
                    ItemSpriteInstance &inst = this->render_held_sprite.get_instance( mob.held_parts[ 0 ] );
                    inst.transform = m;
                    inst.light = light;
                }
            }
        }
    }
    this->render_chain.invalidate_all( );
    this->render_held_cube.invalidate_all( );
    this->render_held_sprite.invalidate_all( );
}

void MobManager::draw( const Renderer &renderer, const Shader &shader ) {
    PROFILE_SCOPE( "mobs_draw" );
    if ( !this->render_inited ) {
        return;
    }
    // Mob parts draw double-sided (Java renders entities entityCutoutNoCull,
    // Bedrock gives leg bones a no-cull material): some skins leave cube
    // faces untextured — the chicken leg only paints its back + bottom rects
    // — so the far side's interior must show through the near face's alpha
    // cutout. The cull-face mode is untouched, so the water-reflection
    // pass's GL_FRONT setting survives the re-enable.
    glDisable( GL_CULL_FACE );
    // Feet rest exactly on the block top face — a small depth bias keeps the
    // coplanar foot rects (chicken toes) from z-fighting the ground.
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset( -1.0f, -1.0f );
    this->render_chain.draw( renderer, shader );
    glPolygonOffset( 0.0f, 0.0f );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_CULL_FACE );
}

void MobManager::draw_held( const Renderer &renderer, const Shader &block_object_shader, const Shader &item_sprite_shader ) {
    PROFILE_SCOPE( "mobs_draw" );
    if ( !this->render_inited ) {
        return;
    }
    this->render_held_cube.draw( renderer, block_object_shader );
    this->render_held_sprite.draw( renderer, item_sprite_shader );
}

void MobManager::cleanup( ) {
    this->render_chain.clear( );
    this->render_held_cube.clear( );
    this->render_held_sprite.clear( );
    this->mobs.clear( );
}
