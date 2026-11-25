#include "common/mobs.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <stdlib.h>
#include <unordered_map>

#include "common/constants.hpp"
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

// Post-integration piston-contact pass over owned, live mobs: each mover's
// overlap correction is terrain-resolved (apply_moving_box_push), so a
// wedged mob stays wedged rather than tunneling. Mirrors, remote-avatar
// entries, seated/no_ai/dead mobs are the authority of someone (or nothing)
// else and are never locally repositioned.
void MobManager::push_with_moving_boxes( World &world, const MovingBoxPushSet &push ) {
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( Mob &mob : this->mobs ) {
        if ( mob.death_ticks > 0 || mob.seated || mob.no_ai || mob.net.mirror || mob.uid != 0 ) {
            continue;
        }
        const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( mob.pos / csize ) );
        const Chunk *chunk = world.chunkLoader.get_chunk( feet_chunk );
        if ( chunk == nullptr || chunk->load_state != CHUNK_LIVE ) {
            continue;
        }
        const MobTypeDef &def = mob_types[ mob.type ];
        const double scale = scale_of( mob );
        const double half = def.hitbox_w * scale * 0.5;
        const double height = def.hitbox_h * scale;
        glm::dvec3 min( mob.pos.x - half, mob.pos.y, mob.pos.z - half );
        glm::dvec3 max( mob.pos.x + half, mob.pos.y + height, mob.pos.z + half );
        const glm::dvec3 before = min;
        for ( const MovingBodyBox &moving : push.movers ) {
            Collision::apply_moving_box_push( world, moving, min, max, push.collision );
        }
        mob.pos += min - before;
    }
}

// Network-driven pose: ease toward the latest target (snap over 8 blocks),
// accumulate walk_dist from the applied horizontal motion, drive leg_amp
// like Java's limbSwingAmount, and keep the head within 50° of the body.
void MobManager::tick_remote( Mob &mob ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    mob.prev_sneak = mob.sneak;
    // Avatar hurt flash (flash_remote_hurt) uses the same timer as local mobs.
    if ( mob.hurt_ticks > 0 ) {
        mob.hurt_ticks--;
    }
    const glm::dvec3 delta = mob.target - mob.pos;
    if ( glm::length( delta ) > 8.0 ) {
        mob.pos = mob.target;
        // Teleport-sized jump (also the first pose): snap the sneak amount
        // so scripted poses don't need settle ticks.
        mob.sneak = mob.sneak_target;
        mob.prev_sneak = mob.sneak;
    } else {
        mob.pos += delta * 0.35;
    }
    const glm::dvec3 moved = mob.pos - mob.prev_pos;
    const double hd = std::hypot( moved.x, moved.z );
    mob.walk_dist += static_cast<float>( hd );
    const float amp_target = static_cast<float>( fmin( hd * ( UPS_RATE / 20.0 ) * 4.0, 1.0 ) );
    mob.leg_amp += ( amp_target - mob.leg_amp ) * 0.2f;
    mob.sneak += ( mob.sneak_target - mob.sneak ) * 0.35f;

    if ( mob.seated ) {
        // Seated in a minecart: the body faces the cart's yaw (head keeps
        // the streamed look target), legs stay folded — no gait/crouch.
        mob.yaw += wrap_degrees_180( mob.seat_yaw - mob.yaw ) * 0.35f;
        mob.leg_amp = 0.0f;
        mob.sneak = mob.prev_sneak = mob.sneak_target = 0.0f;
    } else {
        if ( hd > 1e-4 ) {
            // While moving, the body eases toward the travel direction.
            const float want_yaw = static_cast<float>( atan2( moved.x, -moved.z ) * ( 180.0 / M_PI ) );
            mob.yaw += wrap_degrees_180( want_yaw - mob.yaw ) * 0.2f;
        }
        // The head can't lead the body by more than 50°: drag the body along.
        const float lead = wrap_degrees_180( mob.look_yaw - mob.yaw );
        if ( lead > 50.0f ) {
            mob.yaw = wrap_degrees_180( mob.look_yaw - 50.0f );
        } else if ( lead < -50.0f ) {
            mob.yaw = wrap_degrees_180( mob.look_yaw + 50.0f );
        }
    }
    mob.head_yaw = wrap_degrees_180( mob.look_yaw - mob.yaw );
    // angle_V > 0 looks down; the head animator's pitch > 0 looks up.
    mob.head_pitch = -mob.look_pitch;
}

void MobManager::tick_mob( World &world, Mob &mob, const MobTypeDef &def, const glm::dvec3 &player_pos, const MobTickInput *in, MobTickOutput *out ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    // Wing-flap oscillator: gentle bob on the ground, fast flap airborne.
    mob.wing_phase += mob.standing ? 0.06f : 0.4f;
    mob.anim_phase += 1.0f;
    if ( mob.hurt_ticks > 0 ) {
        mob.hurt_ticks--;
    }
    if ( mob.death_ticks > 0 ) {
        mob.death_ticks++;
        // A hovering mob falls out of the air while it tips over.
        if ( def.hover_h > 0.0f && !mob.standing ) {
            mob.vel.x = mob.vel.z = 0.0;
            mob.vel.y = fmax( mob.vel.y - GRAVITY_STRENGTH, -TERMINAL_VELOCITY );
            glm::dvec3 eye = mob.pos + glm::dvec3( 0.0, def.hitbox_h, 0.0 );
            glm::dvec3 move = mob.vel;
            int standing = 0;
            Collision::check_move( world, move, eye, &standing, def.hitbox_h, def.hitbox_h, false, def.hitbox_w / 2.0,
                                   &world.bodies.piston_collision_boxes( ) );
            mob.pos += move;
            mob.standing = standing;
            mob.vel.y = standing ? 0.0 : move.y;
        }
        return;
    }

    // Breeding timers: love lasts until mating clears it, the post-mating
    // cooldown runs down, and a baby counts up to adult (feeding
    // accelerates it).
    if ( mob.breed_cooldown > 0 ) {
        mob.breed_cooldown--;
    }
    if ( mob.age_ticks < 0 && ++mob.age_ticks == 0 ) {
        mob.needs_unstick = 1;
    }
    // Grow-up doubles the hitbox at once: a baby pressed against a wall or
    // fence now overlaps the block, and check_move only clamps faces a move
    // crosses, so the mob would tunnel straight through on its next step.
    // Push it back out to free space before physics runs.
    if ( mob.needs_unstick ) {
        mob.needs_unstick = 0;
        glm::dvec3 eye = mob.pos + glm::dvec3( 0.0, def.hitbox_h, 0.0 );
        if ( Collision::unstick( world, eye, def.hitbox_h, def.hitbox_h, def.hitbox_w / 2.0 ) ) {
            mob.pos = eye - glm::dvec3( 0.0, def.hitbox_h, 0.0 );
        }
    }

    // Block-world showcase mobs: the snapshot, hurt countdown and death
    // tip-over above still run; everything below (targeting, daylight burn,
    // grazing, look-at, wander/panic, physics, knockback, ambient sounds) is
    // skipped so they simply stand where they were placed.
    if ( mob.no_ai ) {
        return;
    }

    const glm::ivec3 feet = glm::ivec3( glm::floor( mob.pos ) );
    const bool in_water = world.get_loaded_block( feet ).id == WATER || world.get_loaded_block( feet + glm::ivec3( 0, 1, 0 ) ).id == WATER;
    const bool hostile = def.ai == MOB_AI_HOSTILE_MELEE || def.ai == MOB_AI_HOSTILE_CREEPER || def.ai == MOB_AI_HOSTILE_RANGED ||
                         def.ai == MOB_AI_HOSTILE_BLAZE || def.ai == MOB_AI_HOSTILE_GHAST;
    // in==nullptr defaults: not targetable, noon (no burn), standing eye height.
    static const MobTickInput fallback_input = { false, DAY_LENGTH_TICKS / 4, PLAYER_EYE_HEIGHT };
    if ( in == nullptr ) {
        in = &fallback_input;
    }
    const glm::dvec3 player_feet = player_pos - glm::dvec3( 0.0, in->player_eye_height, 0.0 );
    const double player_dist_xz = std::hypot( player_feet.x - mob.pos.x, player_feet.z - mob.pos.z );

    // Nearest targetable player among the local player and the remote
    // targets (multiplayer): hostiles chase whoever that is.
    glm::dvec3 target_feet = player_feet;
    double target_dist_xz = player_dist_xz;
    int32_t target_player_id = -1;               // -1 = the local player
    double target_eye_h = in->player_eye_height; // the ranged AI's aim point
    if ( !in->player_targetable ) {
        target_dist_xz = 1e30;
    }
    for ( const MobRemoteTarget &remote : in->remote_targets ) {
        if ( !remote.targetable ) {
            continue;
        }
        const glm::dvec3 remote_feet = remote.eye_pos - glm::dvec3( 0.0, remote.eye_height, 0.0 );
        const double d = std::hypot( remote_feet.x - mob.pos.x, remote_feet.z - mob.pos.z );
        if ( d < target_dist_xz ) {
            target_dist_xz = d;
            target_feet = remote_feet;
            target_player_id = remote.player_id;
            target_eye_h = remote.eye_height;
        }
    }
    const bool any_targetable = target_dist_xz < 1e29;

    // Hostile targeting: acquire inside 16 blocks of the target's feet, drop
    // past 24. No targetable player means passive wandering. The ghast
    // fires from much further out, so it acquires and keeps targets at
    // roughly double those distances.
    if ( hostile ) {
        const double acquire = def.ai == MOB_AI_HOSTILE_GHAST ? 32.0 : 16.0;
        const double drop = def.ai == MOB_AI_HOSTILE_GHAST ? 48.0 : 24.0;
        if ( !any_targetable ) {
            mob.targeting = 0;
        } else if ( !mob.targeting && target_dist_xz < acquire ) {
            mob.targeting = 1;
        } else if ( mob.targeting && target_dist_xz > drop ) {
            mob.targeting = 0;
        }
    }

    // Zombies and skeletons burn in daylight under open sky: 1 damage per
    // second with the hurt flash, but no knockback and no panic. Nether
    // types never burn (burns_in_daylight == 0).
    if ( def.burns_in_daylight && !in_water ) {
        const glm::dvec3 head = mob.pos + glm::dvec3( 0.0, def.hitbox_h, 0.0 );
        const float sky = world.chunkLoader.light_sample( glm::vec3( head ) ).x;
        if ( mob_burns_in_daylight( light_daylight_factor( in->world_time ), sky ) ) {
            if ( ++mob.burn_acc >= UPS_RATE ) {
                mob.burn_acc = 0;
                mob.health -= 1.0f;
                mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
                const double dist = glm::distance( mob.pos, player_pos );
                const float gain = audio_distance_gain( dist );
                if ( mob.health <= 0.0f ) {
                    // Frozen where it stands — see hurt()'s kill path for
                    // why the streamed vel must be zero.
                    mob.vel = glm::dvec3( 0.0 );
                    mob.kb = glm::dvec3( 0.0 );
                    mob.death_ticks = 1;
                    if ( def.sound_death != NULL ) {
                        audio_play_sfx_gain( def.sound_death, gain );
                    }
                    return;
                }
                if ( def.sound_hurt != NULL ) {
                    audio_play_sfx_gain( def.sound_hurt, gain );
                }
            }
        } else {
            mob.burn_acc = 0;
        }
    }

    // Grazing (shearable types — the sheep): a shorn mob standing on grass
    // dips its head for ~2 s (graze_ticks) then eats the block underfoot;
    // grazed cell lands in MobTickOutput::grazes and the caller converts it
    // to dirt — same hand-off as tramples. A started graze pins the AI and
    // the look-at below; panic or losing the ground aborts it. The finish
    // clears `sheared` (wool regrows) and, for a baby, sheds ~10% of the
    // remaining growth like a feed.
    bool grazing = false;
    if ( def.shear_drop.item != LAST_BLOCK_ID && !in_water ) {
        const glm::ivec3 under = glm::ivec3( glm::floor( mob.pos - glm::dvec3( 0.0, 0.05, 0.0 ) ) );
        if ( mob.graze_ticks > 0 ) {
            if ( mob.standing && mob.panic_ticks <= 0 ) {
                grazing = true;
                if ( --mob.graze_ticks == 0 && world.get_loaded_block( under ).id == GRASS ) {
                    if ( out != nullptr ) {
                        out->grazes.push_back( under );
                    }
                    mob.sheared = 0;
                    if ( mob.age_ticks < 0 ) {
                        mob.age_ticks = glm::min( 0, mob.age_ticks - mob.age_ticks / 10 + 1 );
                        mob.needs_unstick = mob.age_ticks == 0;
                    }
                    const double graze_dist = glm::distance( mob.pos, player_feet );
                    audio_play_sfx_gain( "dig_grass", audio_distance_gain( graze_dist ) );
                }
            } else {
                mob.graze_ticks = 0;
            }
        }
        if ( !grazing && mob.sheared && mob.standing && mob.panic_ticks <= 0 && mob.love_ticks <= 0 && mob.ai_state == 0 &&
             world.get_loaded_block( under ).id == GRASS ) {
            std::uniform_real_distribution<double> roll( 0.0, 1.0 );
            const double p = 1.0 / ( 2.5 * UPS_RATE );
            if ( roll( this->rng ) < p ) {
                mob.graze_ticks = MOB_GRAZE_TICKS;
                grazing = true;
            }
        }
    }

    // Nearest player look-at (single-player: the one camera position). A
    // grazing mob keeps its head down on the grass instead.
    const glm::dvec3 mob_eye = mob.pos + glm::dvec3( 0.0, def.eye_h * scale_of( mob ), 0.0 );
    if ( grazing ) {
        mob.head_yaw += ( 0.0f - mob.head_yaw ) * 0.2f;
        mob.head_pitch += ( MOB_GRAZE_PITCH - mob.head_pitch ) * 0.2f;
    } else {
        const glm::dvec3 to_player = player_pos - mob_eye;
        const double dist_xz = std::hypot( to_player.x, to_player.z );
        float want_yaw = 0.0f, want_pitch = 0.0f;
        if ( glm::length( to_player ) < 8.0 && dist_xz > 1e-3 ) {
            const float pyaw = static_cast<float>( atan2( to_player.x, -to_player.z ) * ( 180.0 / M_PI ) );
            want_yaw = glm::clamp( wrap_degrees_180( pyaw - mob.yaw ), -45.0f, 45.0f );
            want_pitch = glm::clamp( static_cast<float>( atan2( to_player.y, dist_xz ) * ( 180.0 / M_PI ) ), -45.0f, 45.0f );
        }
        mob.head_yaw += ( want_yaw - mob.head_yaw ) * 0.2f;
        mob.head_pitch += ( want_pitch - mob.head_pitch ) * 0.2f;
    }

    float speed = 0.0f;
    // The walk direction: mob.yaw normally, but a strafing ranged hostile
    // slides sideways on strafe_yaw while its body stays on the target.
    bool strafing = false;
    float strafe_yaw = 0.0f;
    if ( hostile && mob.targeting ) {
        // Chase: face the target's feet, turning at most 10°/tick.
        const glm::dvec2 to_player( target_feet.x - mob.pos.x, target_feet.z - mob.pos.z );
        const float want_yaw = static_cast<float>( atan2( to_player.x, -to_player.y ) * ( 180.0 / M_PI ) );
        if ( def.ai == MOB_AI_HOSTILE_RANGED ) {
            mob.yaw += glm::clamp( wrap_degrees_180( want_yaw - mob.yaw ), -10.0f, 10.0f );
            // Stand-off strafe: approach beyond FAR, circle at ±65° inside
            // the band, drift back diagonally under NEAR. strafe_dir flips
            // on a timer so the orbit reads as circling, not jitter.
            if ( --mob.strafe_ticks <= 0 ) {
                std::uniform_int_distribution<int> span( UPS_RATE, 3 * UPS_RATE );
                mob.strafe_ticks = span( this->rng );
                mob.strafe_dir = ( this->rng( ) & 1 ) ? 1 : -1;
            }
            strafing = true;
            strafe_yaw = want_yaw;
            if ( target_dist_xz < MOB_RANGED_NEAR ) {
                strafe_yaw += mob.strafe_dir * 135.0f;
            } else if ( target_dist_xz < MOB_RANGED_FAR ) {
                strafe_yaw += mob.strafe_dir * 65.0f;
            }
            speed = def.chase_speed;
            // Bow: ~2 s between shots, and only with a clear pick-ray line
            // to the target's chest — a walled-off skeleton keeps circling
            // instead of plinking the wall between them.
            if ( mob.attack_ticks > 0 ) {
                mob.attack_ticks--;
            }
            if ( mob.attack_ticks <= 0 && target_dist_xz < MOB_RANGED_SHOT_RANGE ) {
                const glm::dvec3 aim = target_feet + glm::dvec3( 0.0, target_eye_h * 0.75, 0.0 );
                glm::ivec3 los_cell;
                int los_face = 0;
                if ( !RayTraversal::find_block_from_to( world, mob_eye, aim, los_cell, &los_face ) ) {
                    // Aim jitter grows with range — a laser-locked skeleton
                    // would be undodgeable.
                    std::uniform_real_distribution<double> spread( -0.2 - target_dist_xz * 0.04, 0.2 + target_dist_xz * 0.04 );
                    const glm::dvec3 aim_at = aim + glm::dvec3( spread( this->rng ), 0.0, spread( this->rng ) );
                    const glm::dvec3 vel = arrow_aim_vel( mob_eye, aim_at, MOB_ARROW_SPEED );
                    if ( out != nullptr ) {
                        out->arrow_shots.push_back( { mob_eye + glm::normalize( vel ) * 0.5, vel, def.attack_damage } );
                    }
                    audio_play_sfx_gain( "bow_shoot", audio_distance_gain( glm::distance( mob.pos, player_pos ) ) );
                    std::uniform_real_distribution<double> cooldown( 1.6, 2.4 );
                    mob.attack_ticks = static_cast<int>( cooldown( this->rng ) * UPS_RATE );
                }
            }
        } else if ( def.ai == MOB_AI_HOSTILE_BLAZE || def.ai == MOB_AI_HOSTILE_GHAST ) {
            const bool ghast = def.ai == MOB_AI_HOSTILE_GHAST;
            mob.yaw += glm::clamp( wrap_degrees_180( want_yaw - mob.yaw ), -10.0f, 10.0f );
            // Same circling stand-off band as the archer skeleton — the
            // blaze floats just off the ground while it closes in. The
            // ghast keeps its distance on a wider band and fires from
            // further out.
            if ( --mob.strafe_ticks <= 0 ) {
                std::uniform_int_distribution<int> span( UPS_RATE, 3 * UPS_RATE );
                mob.strafe_ticks = span( this->rng );
                mob.strafe_dir = ( this->rng( ) & 1 ) ? 1 : -1;
            }
            strafing = true;
            strafe_yaw = want_yaw;
            const double strafe_near = ghast ? 12.0 : MOB_RANGED_NEAR;
            const double strafe_far = ghast ? 24.0 : MOB_RANGED_FAR;
            if ( target_dist_xz < strafe_near ) {
                strafe_yaw += mob.strafe_dir * 135.0f;
            } else if ( target_dist_xz < strafe_far ) {
                strafe_yaw += mob.strafe_dir * 65.0f;
            }
            speed = def.chase_speed;
            if ( mob.attack_ticks > 0 ) {
                mob.attack_ticks--;
            }
            const glm::dvec3 aim = target_feet + glm::dvec3( 0.0, target_eye_h * 0.75, 0.0 );
            if ( mob.volley_ticks > 0 ) {
                // Mid-volley: the blaze fires 3 fireballs ~5 ticks apart;
                // the ghast spends the window as a ~0.4 s screamed charge
                // and releases one fireball on the last tick.
                const bool fire_tick = ghast ? mob.volley_ticks == 5 : mob.volley_ticks % 5 == 0;
                if ( fire_tick ) {
                    std::uniform_real_distribution<double> spread( -0.15, 0.15 );
                    const glm::dvec3 aim_at = aim + glm::dvec3( spread( this->rng ), spread( this->rng ), spread( this->rng ) );
                    const glm::dvec3 vel = glm::normalize( aim_at - mob_eye ) * ArrowEntities::ARROW_SPEED * 0.8;
                    if ( out != nullptr ) {
                        // The ghast's hitbox is 4 blocks wide: the fireball
                        // must spawn outside its own body, from its face.
                        const double launch = ghast ? def.hitbox_w / 2.0 + 0.5 : 0.5;
                        out->arrow_shots.push_back( { mob_eye + glm::normalize( vel ) * launch, vel, def.attack_damage, 1 } );
                    }
                    if ( ghast ) {
                        audio_play_sfx_gain( "ghast_shoot", audio_distance_gain( glm::distance( mob.pos, player_pos ) ) );
                    }
                }
                if ( --mob.volley_ticks == 0 ) {
                    // Cooldown after the volley (~4 s).
                    std::uniform_real_distribution<double> cooldown( 3.2, 4.4 );
                    mob.attack_ticks = static_cast<int>( cooldown( this->rng ) * UPS_RATE );
                }
            } else if ( !ghast && target_dist_xz < def.hitbox_w / 2.0 + 0.3 + 0.5 && fabs( target_feet.y - mob.pos.y ) < 1.5 ) {
                // Pressed against the player: contact hit instead of a volley.
                mob.attack_ticks = UPS_RATE;
                if ( out != nullptr ) {
                    out->player_hits.push_back( { mob.pos, def.attack_damage, target_player_id } );
                }
            } else if ( mob.attack_ticks <= 0 && target_dist_xz < ( ghast ? MOB_GHAST_SHOT_RANGE : MOB_RANGED_SHOT_RANGE ) ) {
                glm::ivec3 los_cell;
                int los_face = 0;
                if ( !RayTraversal::find_block_from_to( world, mob_eye, aim, los_cell, &los_face ) ) {
                    mob.volley_ticks = ghast ? 60 : 15; // blaze shots at 15, 10, 5; the ghast's lands at 5
                    audio_play_sfx_gain( ghast ? "ghast_charge" : "blaze_shoot", audio_distance_gain( glm::distance( mob.pos, player_pos ) ) );
                }
            }
        } else {
            mob.yaw += glm::clamp( wrap_degrees_180( want_yaw - mob.yaw ), -10.0f, 10.0f );
            speed = def.chase_speed;
            if ( def.ai == MOB_AI_HOSTILE_MELEE ) {
                // Melee reach: just past the hitbox edge, with a 1 s cooldown.
                if ( mob.attack_ticks > 0 ) {
                    mob.attack_ticks--;
                }
                if ( mob.attack_ticks <= 0 && target_dist_xz < def.hitbox_w / 2.0 + 0.3 + 0.5 && fabs( target_feet.y - mob.pos.y ) < 1.5 ) {
                    mob.attack_ticks = UPS_RATE;
                    if ( out != nullptr ) {
                        out->player_hits.push_back( { mob.pos, def.attack_damage, target_player_id } );
                    }
                }
            } else {
                // Creeper fuse: lit inside 3 blocks; the mob stands still while
                // fusing and defuses (counts back down) past 7 blocks. The tick
                // loop explodes it at the end of the fuse.
                if ( mob.fuse_ticks > 0 || target_dist_xz < 3.0 ) {
                    speed = 0.0f;
                    if ( mob.fuse_ticks == 0 ) {
                        mob.fuse_ticks = 1;
                        audio_play_sfx_gain( "fuse", audio_distance_gain( glm::distance( mob.pos, player_pos ) ) );
                    }
                }
            }
        }
    }
    // A lit creeper always ticks its fuse: up while in range, down (defuse)
    // when the player left or stopped being targetable.
    if ( def.ai == MOB_AI_HOSTILE_CREEPER && mob.fuse_ticks > 0 ) {
        if ( mob.targeting && target_dist_xz <= 7.0 ) {
            mob.fuse_ticks++;
        } else {
            mob.fuse_ticks--;
        }
    }
    if ( !grazing && ( def.ai == MOB_AI_PASSIVE_WANDER || ( hostile && !mob.targeting ) ) ) {
        // Love mode: the nearest same-type partner also in love. Seeking it
        // outranks tempting and wandering below; with no partner the mob
        // keeps its normal behavior instead of standing still.
        const Mob *mate = nullptr;
        double mate_dist = 1e30;
        if ( mob.love_ticks > 0 ) {
            for ( const Mob &other : this->mobs ) {
                if ( &other == &mob || other.type != mob.type || other.love_ticks <= 0 || //
                     other.death_ticks > 0 || other.uid != 0 || other.net.mirror ) {
                    continue;
                }
                const double d = glm::distance( other.pos, mob.pos );
                if ( d < mate_dist ) {
                    mate_dist = d;
                    mate = &other;
                }
            }
        }
        if ( mob.panic_ticks > 0 ) {
            // Panic: sprint in a fleeing direction, re-picking occasionally.
            mob.panic_ticks--;
            if ( mob.panic_ticks % 15 == 0 ) {
                std::uniform_real_distribution<float> pick_yaw( -180.0f, 180.0f );
                mob.yaw = pick_yaw( this->rng );
            }
            speed = def.panic_speed;
        } else if ( mate != nullptr ) {
            // A mate is available: path to it. The pair check in tick()
            // produces the baby on contact.
            if ( mate_dist > def.hitbox_w * 0.5 ) {
                const glm::dvec2 to_mate( mate->pos.x - mob.pos.x, mate->pos.z - mob.pos.z );
                const float want_yaw = static_cast<float>( atan2( to_mate.x, -to_mate.y ) * ( 180.0 / M_PI ) );
                mob.yaw += glm::clamp( wrap_degrees_180( want_yaw - mob.yaw ), -6.0f, 6.0f );
                speed = def.walk_speed;
            }
            mob.ai_state = 0;
            mob.ai_ticks = 0;
        } else if ( !in->player_no_clip && mob_def_is_breed_item( def, in->held_item ) && //
                    player_dist_xz < MOB_TEMPT_RANGE && fabs( player_feet.y - mob.pos.y ) < 4.0 ) {
            // Tempted: follow the player holding this animal's food,
            // stopping just short. Resets the wander timer so the mob
            // returns to idling cleanly when the food goes away.
            if ( player_dist_xz > 1.6 ) {
                const glm::dvec2 to_player( player_feet.x - mob.pos.x, player_feet.z - mob.pos.z );
                const float want_yaw = static_cast<float>( atan2( to_player.x, -to_player.y ) * ( 180.0 / M_PI ) );
                mob.yaw += glm::clamp( wrap_degrees_180( want_yaw - mob.yaw ), -6.0f, 6.0f );
                speed = def.walk_speed;
            }
            mob.ai_state = 0;
            mob.ai_ticks = 0;
        } else if ( mob.ai_state == 0 ) {
            // Idle, then pick a wander target: a random column within 8
            // blocks, validated against the terrain the mob can cross.
            if ( --mob.ai_ticks <= 0 ) {
                std::uniform_real_distribution<double> off( -8.0, 8.0 );
                const double tx = mob.pos.x + off( this->rng );
                const double tz = mob.pos.z + off( this->rng );
                const int bx = static_cast<int>( floor( tx ) );
                const int bz = static_cast<int>( floor( tz ) );
                bool found = false;
                double ground;
                if ( def.hover_h > 0.0f ) {
                    // A flyer climbs and dives at will, so any column with
                    // ground inside a tall window is fair game — the probe
                    // reads the surface it would hover over (water counts).
                    // Symmetric reach matters: a shallow probe strands a
                    // bee on whatever hilltop it last climbed, since every
                    // lowland pick reads as "no ground".
                    ground = mob_terrain::hover_ground_at( world, bx, bz, static_cast<int>( floor( mob.pos.y ) ) + 6, 30, &found );
                } else {
                    // Walkers: the pick has to be reachable terrain — at
                    // most one hop up (+1) or a safe drop down (-3). A
                    // steeper hillside reads as its inside-the-slope face
                    // (the scan's first "solid" is underground), which
                    // lands outside the band and is rejected like any
                    // cliff; without the bound every uphill pick reports
                    // "ground" and the random walk ratchets mobs onto
                    // hilltops. The band is compared with slack: the feet
                    // rest an epsilon above the block top after settling.
                    ground = mob_terrain::ground_top_at( world, bx, bz, static_cast<int>( floor( mob.pos.y ) ) + 2, 6, &found );
                    const double dy = ground - mob.pos.y;
                    if ( found && ( dy > 1.0 + 1e-3 || dy < -( 3.0 + 1e-3 ) ) ) {
                        found = false;
                    }
                    // No wading picks: the probe's "ground" under a liquid
                    // is the bottom, which would send the mob walking into
                    // oceans and lava seas.
                    if ( found ) {
                        const BlockID feet_id = world.get_loaded_block( glm::ivec3( bx, static_cast<int>( ground ), bz ) ).id;
                        if ( feet_id == WATER || feet_id == LAVA ) {
                            found = false;
                        }
                    }
                }
                if ( found ) {
                    mob.target = glm::dvec3( tx, ground, tz );
                    mob.ai_state = 1;
                    mob.leg_locked = 0;
                    mob.blocked_ticks = 0;
                    // Bound the leg by its walk (+ slack for bumps), not a
                    // flat 20 s: a locked heading can't re-aim, so a leg
                    // shouldn't drift far past the pick once overshot.
                    const long leg_ticks =
                        def.walk_speed > 1e-6f
                            ? static_cast<long>( glm::distance( mob.pos, mob.target ) / def.walk_speed * 1.5 ) + UPS_RATE
                            : 20L * UPS_RATE;
                    mob.ai_ticks = static_cast<int>( std::min( 20L * UPS_RATE, leg_ticks ) );
                } else {
                    std::uniform_int_distribution<int> idle_ticks( 2 * UPS_RATE, 6 * UPS_RATE );
                    mob.ai_ticks = idle_ticks( this->rng );
                }
            }
        } else {
            // Wandering: align to the pick briefly (<=4 deg/tick), then
            // commit to the heading and walk it straight — a constant-
            // velocity leg dead-reckons perfectly for multiplayer receivers
            // and reads deliberate in person. Ends on arrival, overshoot
            // (the mob has passed the target), or the timeout. A pinned leg
            // (>~0.5 s blocked) stops pushing and stands it out instead of
            // streaming dead-reckon corrections for a mob going nowhere.
            const glm::dvec2 to_target( mob.target.x - mob.pos.x, mob.target.z - mob.pos.z );
            const double dist = glm::length( to_target );
            if ( mob.blocked_ticks > UPS_RATE / 2 ) {
                speed = 0.0f;
            } else {
                if ( !mob.leg_locked ) {
                    const float want_yaw = static_cast<float>( atan2( to_target.x, -to_target.y ) * ( 180.0 / M_PI ) );
                    const float dyaw = wrap_degrees_180( want_yaw - mob.yaw );
                    mob.yaw += glm::clamp( dyaw, -4.0f, 4.0f ); // 4 deg/tick max turn
                    if ( fabsf( dyaw ) <= 2.0f ) {
                        mob.leg_locked = true;
                    }
                }
                speed = def.walk_speed;
            }
            // Overshoot is only meaningful once vel is the committed leg
            // heading — stale vel from before the pick can point anywhere.
            const bool overshot = mob.leg_locked && ( to_target.x * mob.vel.x + to_target.y * mob.vel.z ) < 0.0;
            if ( dist < 0.6 || overshot || --mob.ai_ticks <= 0 ) {
                mob.ai_state = 0;
                mob.leg_locked = 0;
                mob.blocked_ticks = 0;
                std::uniform_int_distribution<int> idle_ticks( 2 * UPS_RATE, 6 * UPS_RATE );
                mob.ai_ticks = idle_ticks( this->rng );
            }
        }
    }

    if ( speed > 0.0f ) {
        // Strafing ranged hostiles move on strafe_yaw while the body faces
        // the target; everything else walks where it looks.
        const float rad = ( strafing ? strafe_yaw : mob.yaw ) * static_cast<float>( M_PI / 180.0 );
        mob.vel.x = sinf( rad ) * speed;
        mob.vel.z = -cosf( rad ) * speed;
    } else {
        mob.vel.x = 0.0;
        mob.vel.z = 0.0;
    }

    // Gravity and buoyancy on the player constants: water drags vertical
    // speed toward a gentle rise so the mob paddles at the surface.
    if ( in_water ) {
        mob.vel.y *= WATER_VERTICAL_DRAG;
        mob.vel.y += 0.003;
        if ( mob.vel.y > SWIM_TERMINAL_VELOCITY ) {
            mob.vel.y = SWIM_TERMINAL_VELOCITY;
        }
        if ( mob.vel.y < -SWIM_TERMINAL_VELOCITY ) {
            mob.vel.y = -SWIM_TERMINAL_VELOCITY;
        }
    } else if ( def.hover_h > 0.0f ) {
        // Hover flight: ease the feet toward hover_h above the highest
        // ground found. The own column scans from the feet down (the floor
        // is always below them); the look-ahead column scans from above the
        // head so a ledge or tree canopy registers as raised ground early
        // and the mob climbs over it instead of bumping the wall — unless
        // the opening under the obstruction fits the mob (a door), which is
        // flown through.
        double ground = 0.0;
        bool found = false;
        const glm::dvec3 ahead = speed > 0.0f ? mob.pos + glm::dvec3( mob.vel.x, 0.0, mob.vel.z ) / static_cast<double>( speed ) * ( def.hitbox_w / 2.0 + 0.6 ) : mob.pos;
        const double pass_h = def.hover_h + def.hitbox_h * scale_of( mob ) + 0.25;
        for ( int s = 0; s < 2; s++ ) {
            if ( s == 1 && speed <= 0.0f ) {
                break;
            }
            const glm::dvec3 col = s == 0 ? mob.pos : ahead;
            const int from_y = s == 0 ? static_cast<int>( floor( mob.pos.y ) ) : static_cast<int>( floor( mob.pos.y + def.hitbox_h + 4.0 ) );
            bool f = false;
            const double g = s == 0 ? mob_terrain::hover_ground_at( world, static_cast<int>( floor( col.x ) ), static_cast<int>( floor( col.z ) ), from_y, 20, &f )
                                    : mob_terrain::hover_ground_ahead_at( world, static_cast<int>( floor( col.x ) ), static_cast<int>( floor( col.z ) ), from_y, 20, pass_h, &f );
            if ( f && ( !found || g > ground ) ) {
                ground = g;
            }
            found = found || f;
        }
        if ( found ) {
            mob.vel.y = glm::clamp( ( ground + def.hover_h - mob.pos.y ) * 0.3, -0.05, 0.09 );
        } else {
            // No ground in range below (void/unloaded): keep falling.
            mob.vel.y = glm::clamp( mob.vel.y - static_cast<double>( GRAVITY_STRENGTH ), -static_cast<double>( TERMINAL_VELOCITY ), static_cast<double>( TERMINAL_VELOCITY ) );
        }
    } else {
        mob.vel.y -= GRAVITY_STRENGTH;
        if ( mob.vel.y > TERMINAL_VELOCITY ) {
            mob.vel.y = TERMINAL_VELOCITY;
        }
        if ( mob.vel.y < -TERMINAL_VELOCITY ) {
            mob.vel.y = -TERMINAL_VELOCITY;
        }
    }
    // Per-type downward speed cap (the chicken's glide).
    if ( def.fall_cap > 0.0f && mob.vel.y < -static_cast<double>( def.fall_cap ) ) {
        mob.vel.y = -def.fall_cap;
    }

    // check_move's position is the eye; for a mob the eye sits at the top of
    // its hitbox (feet + hitbox_h), so eye_height == hitbox height. The
    // knockback impulse rides on top of the AI velocity and decays ~x0.85 per
    // tick (~1 block of total travel from a 0.15 blocks/tick hit).
    const int was_standing = mob.standing;
    const float box_scale = scale_of( mob );
    glm::dvec3 eye = mob.pos + glm::dvec3( 0.0, def.hitbox_h * box_scale, 0.0 );
    glm::dvec3 move = mob.vel + mob.kb;
    mob.kb *= 0.85;
    const glm::dvec3 want_move = mob.vel;
    int standing = 0;
    Collision::check_move( world, move, eye, &standing, def.hitbox_h * box_scale, def.hitbox_h * box_scale, false, def.hitbox_w * box_scale / 2.0,
                           &world.bodies.piston_collision_boxes( ) );
    mob.pos += move;
    mob.standing = standing;

    // Fall tracking feeds the trample landing check: distance only counts
    // while actually descending (jumps and step-ups don't accumulate).
    if ( standing ) {
        if ( !was_standing && mob.fall_dist > 0.5f && out != nullptr ) {
            const glm::ivec3 under = glm::ivec3( glm::floor( mob.pos - glm::dvec3( 0.0, 0.05, 0.0 ) ) );
            if ( world.get_loaded_block( under ).id == FARMLAND ) {
                out->tramples.push_back( under );
            }
        }
        mob.fall_dist = 0.0f;
    } else if ( move.y < 0.0 ) {
        mob.fall_dist += static_cast<float>( -move.y );
    }

    if ( standing ) {
        mob.vel.y = 0.0;
    } else {
        mob.vel.y = move.y;
    }

    // Auto-jump: blocked horizontally while standing with a 1-block wall
    // ahead (something step-up can't climb, but a hop can). The wall's real
    // collision top decides — a fence or closed gate occupies one cell but
    // collides 1.5 high, past the ~1.2 a hop clears, so it must not trigger.
    // Must come after the standing reset so the jump velocity survives to
    // next tick.
    const double want_h = std::hypot( want_move.x, want_move.z );
    const double got_h = std::hypot( move.x, move.z );
    const bool pinned = standing && want_h > 1e-4 && got_h < want_h * 0.3;
    // blocked_ticks latches once a wander leg's pin-stand engages (>~0.5 s):
    // suppressed mobs have want_h == 0, so only the leg's own end clears it.
    if ( pinned && mob.ai_state == 1 ) {
        mob.blocked_ticks++;
    } else if ( mob.blocked_ticks <= UPS_RATE / 2 ) {
        mob.blocked_ticks = 0;
    }
    if ( pinned ) {
        const glm::dvec3 dir( want_move.x / want_h, 0.0, want_move.z / want_h );
        const glm::dvec3 ahead = mob.pos + dir * ( def.hitbox_w / 2.0 + 0.4 );
        const glm::ivec3 cell( static_cast<int>( floor( ahead.x ) ), feet.y, static_cast<int>( floor( ahead.z ) ) );
        if ( mob_terrain::solid_at( world, cell ) && !mob_terrain::solid_at( world, cell + glm::ivec3( 0, 1, 0 ) ) && !mob_terrain::solid_at( world, cell + glm::ivec3( 0, 2, 0 ) ) &&
             Collision::block_collision_top( world, cell ) - mob.pos.y <= 1.0 ) {
            mob.vel.y = JUMP_STRENGTH;
        }
    }

    mob.walk_dist += static_cast<float>( got_h );
    const float amp_target = static_cast<float>( fmin( got_h * ( UPS_RATE / 20.0 ) * 4.0, 1.0 ) );
    mob.leg_amp += ( amp_target - mob.leg_amp ) * 0.15f;

    // Sounds: ambient say every 10-30 s, step every ~0.7 blocks of ground
    // travel — both faded by audio_distance_gain (silent past 64 blocks;
    // audio_play_sfx_gain is a no-op on silent/capture builds).
    const double dist = glm::distance( mob.pos, player_pos );
    if ( def.sound_say != NULL && --mob.say_ticks <= 0 ) {
        std::uniform_int_distribution<int> say( 10 * UPS_RATE, 30 * UPS_RATE );
        mob.say_ticks = say( this->rng );
        audio_play_sfx_gain( def.sound_say, audio_distance_gain( dist ), 0.15f );
    }
    mob.step_acc += static_cast<float>( got_h );
    if ( def.sound_step != NULL && standing && mob.step_acc >= 0.7f ) {
        mob.step_acc = 0.0f;
        audio_play_sfx_gain( def.sound_step, def.step_gain * audio_distance_gain( dist ), 0.1f );
    }
}
