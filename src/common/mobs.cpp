#include "common/mobs.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdlib.h>
#include <unordered_map>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/chunk.hpp"
#include "common/heart_particles.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/light.hpp"
#include "common/mp_test.hpp"
#include "common/net_entities.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/angles.hpp"
#include "common/world.hpp"

// Bedrock modified_distance_moved / Java limbSwing advance about 4 units per
// block; the quadruped walk anim's 38.17°/unit is calibrated against that.
#define MOB_GAIT_UNITS_PER_BLOCK 4.0f

// Breeding timers (UPS ticks): love mode lasts until a mate reaches the
// mob; a mated pair can't re-breed for 2 min; a baby grows up in ~5 min.
#define MOB_BREED_COOLDOWN_TICKS ( 2 * 60 * UPS_RATE )
#define MOB_BABY_AGE_TICKS ( 5 * 60 * UPS_RATE )
// Animals notice a held breed item within this distance of the player.
#define MOB_TEMPT_RANGE 10.0
// Babies render and collide at half the adult hitbox scale.
#define MOB_BABY_SCALE 0.5f
// Breeding hearts: feeding opens a ~5 s window with one heart per ~0.5 s;
// a burst on a newborn, a smaller puff when a baby eats.
#define HEART_TICKS ( 5 * UPS_RATE )
#define HEART_PERIOD_MIN 0.35f
#define HEART_PERIOD_SPREAD 0.5f
#define HEART_BORN_COUNT 7
#define HEART_BABY_FEED_COUNT 2
// Grazing (sheep): a woolly sheep idling on grass starts a ~2 s graze about
// once a minute (Java's 1/1000 chance per 20 Hz tick ≈ 1/(50 s)); a shorn
// one goes for it eagerly (~2.5 s) to get its wool back. graze_ticks pins
// the AI while the head dips.
#define MOB_GRAZE_TICKS ( 2 * UPS_RATE )
#define MOB_GRAZE_PITCH -80.0f // head-down animator pitch while grazing, degrees
// HeartParticles::now wraps with u_Time (3600 s) — wrapped-diff for the
// per-mob emission cadence.
#define HEART_TIME_WRAP 3600.0f

MobManager::MobManager( ) {
    this->enabled = env_bool( "REPGAME_MOBS", true );
    this->spawner_enabled = env_bool( "REPGAME_MOB_SPAWNER", true );
    this->test_mode = env_is_set( "REPGAME_MOB_TEST" );
    this->render_inited = false;
    this->storage = nullptr;
    this->poll_count = 0;
    this->spawn_ticks = 0;
    this->next_uid = 1;
    this->rng.seed( 0x9e3779b9u );
}

void MobManager::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance ) {
    // Rendering is always available (remote players draw even under
    // REPGAME_MOBS=0); `enabled` only gates spawning, ticking and persistence.
    this->render_chain.init( vbl_object_vertex, vbl_mob_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_inited = true;

    // REPGAME_MOB_TEST="pig,x,y,z,yaw;..." — exact spawns for deterministic
    // captures and physics tests.
    const char *spec = env_get( "REPGAME_MOB_TEST" );
    if ( spec != NULL && this->enabled ) {
        char buf[ 1024 ];
        strncpy( buf, spec, sizeof( buf ) - 1 );
        buf[ sizeof( buf ) - 1 ] = 0;
        for ( char *entry = strtok( buf, ";" ); entry != NULL; entry = strtok( NULL, ";" ) ) {
            char type_name[ 32 ];
            float x, y, z, yaw = 0.0f, pitch = 0.0f, sneak = 0.0f;
            if ( sscanf( entry, "%31[^,],%f,%f,%f,%f,%f,%f", type_name, &x, &y, &z, &yaw, &pitch, &sneak ) < 4 ) {
                pr_debug( "REPGAME_MOB_TEST: bad entry '%s' (want type,x,y,z,yaw[,pitch[,sneak]])", entry );
                continue;
            }
            MobType type = MOB_TYPE_COUNT;
            for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
                if ( strcmp( mob_types[ t ].name, type_name ) == 0 ) {
                    type = ( MobType )t;
                }
            }
            if ( type == MOB_TYPE_COUNT ) {
                pr_debug( "REPGAME_MOB_TEST: unknown type '%s'", type_name );
                continue;
            }
            if ( mob_types[ type ].ai == MOB_AI_REMOTE ) {
                // Remote types (player): x,y,z is the feet position; optional
                // pitch is the head's and sneak (0..1) drops the eye height.
                const uint32_t uid = this->spawn_remote( type );
                const float eye_height = PLAYER_EYE_HEIGHT - sneak * PLAYER_SNEAK_EYE_DROP;
                this->set_remote_pose( uid, glm::dvec3( x, y + eye_height, z ), yaw, pitch, eye_height );
                continue;
            }
            // MOB_TEST spawns are non-persistent: they never reach a mobs file
            // and natural spawn/load is disabled for the whole run.
            this->spawn( type, glm::dvec3( x, y, z ), yaw, 0, false );
        }
    }
}

bool MobManager::spawn( MobType type, const glm::dvec3 &pos, float yaw, int persistent, bool capped ) {
    if ( !this->enabled ) {
        return false;
    }
    if ( capped && !this->under_mob_cap( ) ) {
        return false;
    }
    if ( !this->spawn_any( type, pos, yaw, persistent ) ) {
        return false;
    }
    // Multiplayer: announce the spawn (mints a net id). No-op unbound or
    // before WELCOME — those mobs stay local-only like net_id-0 items.
    this->announce_spawn( this->mobs.back( ) );
    return true;
}

bool MobManager::under_mob_cap( ) const {
    int total = 0;
    for ( const Mob &mob : this->mobs ) {
        total += mob.uid == 0 && !mob.net.mirror;
    }
    return total < MAX_MOBS;
}

// Uncapped storage/render spawn: avatars and network mobs (mirrors, owned
// transfers) must always fit. Local population is capped by the callers via
// under_mob_cap(), not here.
bool MobManager::spawn_any( MobType type, const glm::dvec3 &pos, float yaw, int persistent ) {
    const MobTypeDef &def = mob_types[ type ];
    Mob mob;
    mob.type = type;
    mob.variant = def.skin_count > 1 ? static_cast<int>( this->rng( ) % def.skin_count ) : 0;
    mob.pos = pos;
    mob.prev_pos = pos;
    mob.yaw = yaw;
    mob.prev_yaw = yaw;
    mob.vel = glm::dvec3( 0.0 );
    mob.standing = 0;
    mob.health = def.max_health;
    mob.ai_state = 0;
    std::uniform_int_distribution<int> idle_ticks( 2 * UPS_RATE, 6 * UPS_RATE );
    mob.ai_ticks = idle_ticks( this->rng );
    mob.panic_ticks = 0;
    mob.target = pos;
    mob.hurt_ticks = 0;
    mob.death_ticks = 0;
    mob.walk_dist = 0.0f;
    mob.leg_amp = 0.0f;
    mob.head_yaw = 0.0f;
    mob.head_pitch = 0.0f;
    mob.sneak = 0.0f;
    mob.prev_sneak = 0.0f;
    mob.sneak_target = 0.0f;
    mob.persistent = persistent;
    std::uniform_int_distribution<int> say( 10 * UPS_RATE, 30 * UPS_RATE );
    mob.say_ticks = say( this->rng );
    mob.step_acc = 0.0f;
    mob.kb = glm::dvec3( 0.0 );
    mob.despawnable = 0;
    mob.targeting = 0;
    mob.attack_ticks = 0;
    mob.fuse_ticks = 0;
    mob.burn_acc = 0;
    std::uniform_real_distribution<float> phase_pick( 0.0f, 6.2832f );
    mob.wing_phase = phase_pick( this->rng );
    mob.love_ticks = 0;
    mob.breed_cooldown = 0;
    mob.age_ticks = 0;
    mob.sheared = 0;
    mob.graze_ticks = 0;
    mob.needs_unstick = 0;
    mob.fall_dist = 0.0f;
    mob.heart_ticks = 0;
    mob.next_heart_at = 0.0f;
    mob.was_emitting = 0;
    mob.heart_burst = 0;
    mob.uid = 0;
    mob.look_yaw = 0.0f;
    mob.look_pitch = 0.0f;
    mob.net.id = 0;
    mob.net.mirror = false;
    mob.net.chunk = glm::ivec3( glm::floor( pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
    mob.net.pos = pos;
    mob.net.vel = glm::dvec3( 0.0 );
    mob.net_yaw = yaw;
    mob.net_flags = 0;
    mob.net_natural = false;
    mob.net.sent = false;
    mob.net.sent_pos = pos;
    mob.net.sent_vel = glm::dvec3( 0.0 );
    mob.net_sent_yaw = yaw;
    mob.net_sent_health = def.max_health;
    mob.net_sent_flags = 0;
    mob.net.keyframe_tick = -1;
    if ( this->render_inited ) {
        // One render entity per cube; a shearable type allocates for the
        // larger of its two models so a wool-swap never reallocates.
        int cube_count = mob_models[ def.model ].cube_count;
        if ( def.sheared_model != MOB_MODEL_COUNT ) {
            cube_count = std::max( cube_count, mob_models[ def.sheared_model ].cube_count );
        }
        for ( int i = 0; i < cube_count; i++ ) {
            const std::pair<entt::entity, MobPartInstance &> data = this->render_chain.create_instance( );
            mob.parts.push_back( data.first );
        }
    }
    this->mobs.push_back( mob );
    return true;
}

// True while the block at `pos` collides (used for ground probes and the
// auto-jump wall check).
static bool solid_at( World &world, const glm::ivec3 &pos ) {
    const BlockState bs = world.get_loaded_block( pos );
    return bs.id < LAST_BLOCK_ID && block_definition_get_definition( bs.id )->collides_with_player;
}

// Height of the ground surface under the column (bx, bz), scanning from
// `from_y` down; returns the top Y of the first solid block or 0/1 in
// *found. The wander AI rejects targets whose ground drops more than 3 below
// the mob's feet.
static double ground_top_at( World &world, int bx, int bz, int from_y, int max_drop, bool *found ) {
    for ( int y = from_y; y >= from_y - max_drop; y-- ) {
        if ( solid_at( world, glm::ivec3( bx, y, bz ) ) ) {
            *found = true;
            return y + 1.0;
        }
    }
    *found = false;
    return 0.0;
}

// Hover flight's ground probe: like ground_top_at but the water surface also
// counts — a flyer skims the surface instead of dunking into it.
static double hover_ground_at( World &world, int bx, int bz, int from_y, int max_drop, bool *found ) {
    for ( int y = from_y; y >= from_y - max_drop; y-- ) {
        const BlockState bs = world.get_loaded_block( glm::ivec3( bx, y, bz ) );
        if ( bs.id == WATER || ( bs.id < LAST_BLOCK_ID && block_definition_get_definition( bs.id )->collides_with_player ) ) {
            *found = true;
            return y + 1.0;
        }
    }
    *found = false;
    return 0.0;
}

bool MobManager::spawn_in_front( MobType type, const glm::dvec3 &camera_pos, const glm::dvec3 &look, World &world ) {
    const glm::dvec3 at = camera_pos + look * 3.0;
    const int bx = static_cast<int>( floor( at.x ) );
    const int bz = static_cast<int>( floor( at.z ) );
    bool found = false;
    const double ground = ground_top_at( world, bx, bz, static_cast<int>( floor( at.y ) ), 30, &found );
    // Explicit player spawn: uncapped like an egg.
    return this->spawn( type, glm::dvec3( at.x, found ? ground : at.y, at.z ), 0.0f, 1, false );
}

uint32_t MobManager::spawn_remote( MobType type ) {
    // Spawned far below the world: the first set_remote_pose counts as a >8
    // block jump and snaps to the real position.
    if ( !this->spawn_any( type, glm::dvec3( 0.0, -256.0, 0.0 ), 0.0f, 0 ) ) {
        return 0;
    }
    Mob &mob = this->mobs.back( );
    mob.uid = this->next_uid++;
    mob.target = mob.pos;
    return mob.uid;
}

void MobManager::set_remote_pose( uint32_t uid, const glm::dvec3 &eye_pos, float look_yaw_deg, float look_pitch_deg, float eye_height ) {
    for ( Mob &mob : this->mobs ) {
        if ( mob.uid == uid ) {
            // Sanity-clamp the sender's eye height; garbage -> standing.
            if ( !std::isfinite( eye_height ) || eye_height < PLAYER_SNEAK_EYE_HEIGHT - 0.05f || eye_height > PLAYER_EYE_HEIGHT + 0.05f ) {
                eye_height = mob_types[ mob.type ].eye_h;
            }
            mob.target = eye_pos - glm::dvec3( 0.0, static_cast<double>( eye_height ), 0.0 );
            mob.look_yaw = look_yaw_deg;
            mob.look_pitch = look_pitch_deg;
            mob.sneak_target = glm::clamp( ( PLAYER_EYE_HEIGHT - eye_height ) / PLAYER_SNEAK_EYE_DROP, 0.0f, 1.0f );
            return;
        }
    }
}

void MobManager::remove_remote( uint32_t uid ) {
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ++it ) {
        if ( it->uid == uid ) {
            this->remove_mob( it );
            return;
        }
    }
}

// Network-driven pose: ease toward the latest target (snap over 8 blocks),
// accumulate walk_dist from the applied horizontal motion, drive leg_amp
// like Java's limbSwingAmount, and keep the head within 50° of the body.
void MobManager::tick_remote( Mob &mob ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    mob.prev_sneak = mob.sneak;
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
    mob.head_yaw = wrap_degrees_180( mob.look_yaw - mob.yaw );
    // angle_V > 0 looks down; the head animator's pitch > 0 looks up.
    mob.head_pitch = -mob.look_pitch;
}

void MobManager::tick_mob( World &world, Mob &mob, const MobTypeDef &def, const glm::dvec3 &player_pos, const MobTickInput *in, MobTickOutput *out ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    // Wing-flap oscillator: gentle bob on the ground, fast flap airborne.
    mob.wing_phase += mob.standing ? 0.06f : 0.4f;
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
            Collision::check_move( world, move, eye, &standing, def.hitbox_h, def.hitbox_h, false, def.hitbox_w / 2.0 );
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

    const glm::ivec3 feet = glm::ivec3( glm::floor( mob.pos ) );
    const bool in_water = world.get_loaded_block( feet ).id == WATER || world.get_loaded_block( feet + glm::ivec3( 0, 1, 0 ) ).id == WATER;
    const bool hostile = def.ai == MOB_AI_HOSTILE_MELEE || def.ai == MOB_AI_HOSTILE_CREEPER;
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
    int32_t target_player_id = -1; // -1 = the local player
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
        }
    }
    const bool any_targetable = target_dist_xz < 1e29;

    // Hostile targeting: acquire inside 16 blocks of the target's feet, drop
    // past 24. No targetable player means passive wandering.
    if ( hostile ) {
        if ( !any_targetable ) {
            mob.targeting = 0;
        } else if ( !mob.targeting && target_dist_xz < 16.0 ) {
            mob.targeting = 1;
        } else if ( mob.targeting && target_dist_xz > 24.0 ) {
            mob.targeting = 0;
        }
    }

    // Zombies burn in daylight under open sky: 1 damage per second with the
    // hurt flash, but no knockback and no panic.
    if ( hostile && def.ai == MOB_AI_HOSTILE_MELEE && !in_water ) {
        const glm::dvec3 head = mob.pos + glm::dvec3( 0.0, def.hitbox_h, 0.0 );
        const float sky = world.chunkLoader.light_sample( glm::vec3( head ) ).x;
        if ( mob_burns_in_daylight( light_daylight_factor( in->world_time ), sky ) ) {
            if ( ++mob.burn_acc >= UPS_RATE ) {
                mob.burn_acc = 0;
                mob.health -= 1.0f;
                mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
                const double dist = glm::distance( mob.pos, player_pos );
                const float gain = glm::clamp( 1.0f - static_cast<float>( dist ) / 16.0f, 0.0f, 1.0f );
                if ( mob.health <= 0.0f ) {
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

    // Grazing (shearable types — the sheep): a mob standing on grass dips
    // its head for ~2 s (graze_ticks) then eats the block underfoot; the
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
                    audio_play_sfx_gain( "dig_grass", glm::clamp( 1.0f - static_cast<float>( graze_dist ) / 16.0f, 0.0f, 1.0f ) );
                }
            } else {
                mob.graze_ticks = 0;
            }
        }
        if ( !grazing && mob.standing && mob.panic_ticks <= 0 && mob.love_ticks <= 0 && mob.ai_state == 0 &&
             world.get_loaded_block( under ).id == GRASS ) {
            std::uniform_real_distribution<double> roll( 0.0, 1.0 );
            const double p = mob.sheared ? 1.0 / ( 2.5 * UPS_RATE ) : 1.0 / ( 50.0 * UPS_RATE );
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
    if ( hostile && mob.targeting ) {
        // Chase: face the target's feet, turning at most 10°/tick.
        const glm::dvec2 to_player( target_feet.x - mob.pos.x, target_feet.z - mob.pos.z );
        const float want_yaw = static_cast<float>( atan2( to_player.x, -to_player.y ) * ( 180.0 / M_PI ) );
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
                    audio_play_sfx( "fuse" );
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
            // Idle, then pick a wander target: a column within 8 blocks whose
            // ground doesn't drop more than 3 below the mob.
            if ( --mob.ai_ticks <= 0 ) {
                std::uniform_real_distribution<double> off( -8.0, 8.0 );
                const double tx = mob.pos.x + off( this->rng );
                const double tz = mob.pos.z + off( this->rng );
                bool found = false;
                const double ground = ground_top_at( world, static_cast<int>( floor( tx ) ), static_cast<int>( floor( tz ) ),
                                                     static_cast<int>( floor( mob.pos.y ) ) + 1, 4, &found );
                if ( found && mob.pos.y - ground <= 3.0 ) {
                    mob.target = glm::dvec3( tx, ground, tz );
                    mob.ai_state = 1;
                    mob.ai_ticks = 20 * UPS_RATE;
                } else {
                    std::uniform_int_distribution<int> idle_ticks( 2 * UPS_RATE, 6 * UPS_RATE );
                    mob.ai_ticks = idle_ticks( this->rng );
                }
            }
        } else {
            // Wandering: turn toward the target, then walk. Give up on a
            // timeout so unreachable targets don't pin the mob forever.
            const glm::dvec2 to_target( mob.target.x - mob.pos.x, mob.target.z - mob.pos.z );
            const double dist = glm::length( to_target );
            const float want_yaw = static_cast<float>( atan2( to_target.x, -to_target.y ) * ( 180.0 / M_PI ) );
            const float dyaw = wrap_degrees_180( want_yaw - mob.yaw );
            mob.yaw += glm::clamp( dyaw, -4.0f, 4.0f );
            speed = def.walk_speed;
            if ( dist < 0.6 || --mob.ai_ticks <= 0 ) {
                mob.ai_state = 0;
                std::uniform_int_distribution<int> idle_ticks( 2 * UPS_RATE, 6 * UPS_RATE );
                mob.ai_ticks = idle_ticks( this->rng );
            }
        }
    }

    if ( speed > 0.0f ) {
        const float rad = mob.yaw * static_cast<float>( M_PI / 180.0 );
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
        // and the mob climbs over it instead of bumping the wall.
        double ground = 0.0;
        bool found = false;
        const glm::dvec3 ahead = speed > 0.0f ? mob.pos + glm::dvec3( mob.vel.x, 0.0, mob.vel.z ) / static_cast<double>( speed ) * ( def.hitbox_w / 2.0 + 0.6 ) : mob.pos;
        for ( int s = 0; s < 2; s++ ) {
            if ( s == 1 && speed <= 0.0f ) {
                break;
            }
            const glm::dvec3 col = s == 0 ? mob.pos : ahead;
            const int from_y = s == 0 ? static_cast<int>( floor( mob.pos.y ) ) : static_cast<int>( floor( mob.pos.y + def.hitbox_h + 4.0 ) );
            bool f = false;
            const double g = hover_ground_at( world, static_cast<int>( floor( col.x ) ), static_cast<int>( floor( col.z ) ), from_y, 20, &f );
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
    Collision::check_move( world, move, eye, &standing, def.hitbox_h * box_scale, def.hitbox_h * box_scale, false, def.hitbox_w * box_scale / 2.0 );
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
    if ( standing && want_h > 1e-4 && got_h < want_h * 0.3 ) {
        const glm::dvec3 dir( want_move.x / want_h, 0.0, want_move.z / want_h );
        const glm::dvec3 ahead = mob.pos + dir * ( def.hitbox_w / 2.0 + 0.4 );
        const glm::ivec3 cell( static_cast<int>( floor( ahead.x ) ), feet.y, static_cast<int>( floor( ahead.z ) ) );
        if ( solid_at( world, cell ) && !solid_at( world, cell + glm::ivec3( 0, 1, 0 ) ) && !solid_at( world, cell + glm::ivec3( 0, 2, 0 ) ) &&
             Collision::block_collision_top( world, cell ) - mob.pos.y <= 1.0 ) {
            mob.vel.y = JUMP_STRENGTH;
        }
    }

    mob.walk_dist += static_cast<float>( got_h );
    const float amp_target = got_h > 1e-4 ? 1.0f : 0.0f;
    mob.leg_amp += ( amp_target - mob.leg_amp ) * 0.15f;

    // Sounds: ambient say every 10-30 s within 16 blocks, step every ~0.7
    // blocks of ground travel within 8 blocks. Distance gain is 1 - d/16;
    // audio_play_sfx_gain is a no-op on silent/capture builds.
    const double dist = glm::distance( mob.pos, player_pos );
    if ( def.sound_say != NULL && --mob.say_ticks <= 0 ) {
        std::uniform_int_distribution<int> say( 10 * UPS_RATE, 30 * UPS_RATE );
        mob.say_ticks = say( this->rng );
        if ( dist < 16.0 ) {
            audio_play_sfx_gain( def.sound_say, 1.0f - static_cast<float>( dist ) / 16.0f );
        }
    }
    mob.step_acc += static_cast<float>( got_h );
    if ( def.sound_step != NULL && standing && mob.step_acc >= 0.7f ) {
        mob.step_acc = 0.0f;
        if ( dist < 8.0 ) {
            audio_play_sfx_gain( def.sound_step, def.step_gain * ( 1.0f - static_cast<float>( dist ) / 16.0f ) );
        }
    }
}

std::vector<MobManager::Mob>::iterator MobManager::remove_mob( std::vector<Mob>::iterator it ) {
    for ( entt::entity part : it->parts ) {
        this->render_chain.remove( part );
    }
    return this->mobs.erase( it );
}

// AABB slab test: ray vs [feet, feet + (w, h, w)] for every live mob.
int MobManager::raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const {
    int best = -1;
    double best_t = max_dist;
    for ( size_t i = 0; i < this->mobs.size( ); i++ ) {
        const Mob &mob = this->mobs[ i ];
        if ( mob.death_ticks > 0 || mob.uid != 0 ) {
            continue;
        }
        const double hw = mob_types[ mob.type ].hitbox_w * scale_of( mob ) / 2.0;
        const glm::dvec3 bmin( mob.pos.x - hw, mob.pos.y, mob.pos.z - hw );
        const glm::dvec3 bmax( mob.pos.x + hw, mob.pos.y + mob_types[ mob.type ].hitbox_h * scale_of( mob ), mob.pos.z + hw );
        double t0 = 0.0;
        if ( RayTraversal::ray_aabb_entry( origin, dir, bmin, bmax, best_t, &t0 ) ) {
            best_t = t0;
            best = static_cast<int>( i );
        }
    }
    if ( best >= 0 && out_dist != nullptr ) {
        *out_dist = best_t;
    }
    return best;
}

// Crosshair target resolution: a mob hitbox on the ray wins over the
// selected block only when its entry distance is nearer than the block
// cell's own slab entry (or no block is selected).
int MobManager::resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                                int selectionInBounds, const glm::ivec3 &pos_destroy, double *out_dist ) const {
    if ( !this->enabled ) {
        return -1;
    }
    double mob_dist = 0.0;
    const int mob = this->raycast( origin, dir, max_dist, &mob_dist );
    if ( mob < 0 ) {
        return -1;
    }
    double block_dist = 0.0;
    const bool block_hit = selectionInBounds && RayTraversal::ray_aabb_entry( origin, dir, glm::dvec3( pos_destroy ), glm::dvec3( pos_destroy ) + glm::dvec3( 1.0 ), max_dist, &block_dist );
    if ( block_hit && block_dist <= mob_dist ) {
        return -1;
    }
    if ( out_dist != nullptr ) {
        *out_dist = mob_dist;
    }
    return mob;
}

// First player interaction with an owned mob: sets the sticky `touched` bit
// and tombstones a natural mob's roll index so later re-rolls skip it. The
// index lands in the home chunk's OwnedChunk when owned; otherwise it goes
// straight into that chunk's sidecar (single player only — in multiplayer
// the server does this bookkeeping when the TOUCHED flag arrives).
void MobManager::mark_touched( Mob &mob ) {
    if ( mob.touched || mob.despawnable || mob.uid != 0 || mob.net.mirror ) {
        return;
    }
    mob.touched = true;
    if ( !net_is_natural_mob_id( mob.natural_id ) ) {
        return;
    }
    const uint8_t index = static_cast<uint8_t>( net_natural_mob_index( mob.natural_id ) );
    const glm::ivec3 home = net_natural_mob_home( mob.natural_id );
    for ( OwnedChunk &owned : this->owned_chunks ) {
        if ( owned.pos == home ) {
            owned.removed.insert( index );
            return;
        }
    }
    if ( this->storage != nullptr && !this->mp_mode ) {
        std::vector<MobFileRecord> records;
        std::vector<uint8_t> removed;
        this->storage->load_mobs( home, records, &removed );
        if ( std::find( removed.begin( ), removed.end( ), index ) == removed.end( ) ) {
            removed.push_back( index );
            this->storage->persist_mobs( home, records, removed );
        }
    }
}

// MC-style knockback: a 0.15 blocks/tick horizontal impulse that decays
// x0.85 per tick (~1 block of travel) plus a 0.07 blocks/tick vertical bump
// (~0.2 block hop under the player gravity constant). Returns true when the
// hit started the death tip-over.
bool MobManager::hurt( int index, float damage, const glm::dvec3 &attacker_pos ) {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return false;
    }
    Mob &mob = this->mobs[ index ];
    if ( mob.death_ticks > 0 ) {
        return false;
    }
    // hurt() is only reached by player hits (local raycast, ENTITY_ACTION on
    // the owning side): environmental damage adjusts health directly.
    this->mark_touched( mob );
    const MobTypeDef &def = mob_types[ mob.type ];
    mob.health -= damage;
    mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
    // Hostiles take knockback but don't panic.
    mob.panic_ticks = def.ai == MOB_AI_PASSIVE_WANDER ? 5 * UPS_RATE : 0;
    mob.ai_state = 0;
    glm::dvec3 away = mob.pos - attacker_pos;
    away.y = 0.0;
    const double len = glm::length( away );
    if ( len > 1e-6 ) {
        mob.kb = away / len * 0.15;
    }
    mob.vel.y += 0.07;
    const double dist = glm::distance( mob.pos, attacker_pos );
    const float gain = glm::clamp( 1.0f - static_cast<float>( dist ) / 16.0f, 0.0f, 1.0f );
    if ( mob.health <= 0.0f ) {
        mob.death_ticks = 1;
        if ( def.sound_death != NULL ) {
            audio_play_sfx_gain( def.sound_death, gain );
        }
        return true;
    }
    if ( def.sound_hurt != NULL ) {
        audio_play_sfx_gain( def.sound_hurt, gain );
    }
    return false;
}

float MobManager::scale_of( const Mob &mob ) {
    return mob.age_ticks < 0 ? MOB_BABY_SCALE : 1.0f;
}

bool MobManager::feedable( const int index, const ItemID item ) const {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return false;
    }
    const Mob &mob = this->mobs[ index ];
    if ( mob.death_ticks > 0 || mob.uid != 0 ) {
        return false;
    }
    const MobTypeDef &def = mob_types[ mob.type ];
    if ( !mob_def_is_breed_item( def, item ) ) {
        return false;
    }
    if ( mob.net.mirror ) {
        // Love/cooldown live on the owning side — a mirror accepts any valid
        // food here and ENTITY_ACTION is validated authoritatively there.
        return true;
    }
    // A baby always eats (growth boost); an adult only while it can enter
    // love mode.
    return mob.age_ticks < 0 || ( mob.love_ticks <= 0 && mob.breed_cooldown <= 0 );
}

// Applies the feed after feedable() passed: a baby sheds ~10% of its
// remaining growth time; an adult enters love mode and stops wandering to
// look for a mate. mark_touched makes the livestock persist like a penned
// natural.
bool MobManager::feed( const int index, const ItemID item ) {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return false;
    }
    Mob &mob = this->mobs[ index ];
    if ( mob.net.mirror || !this->feedable( index, item ) ) {
        return false;
    }
    this->mark_touched( mob );
    if ( mob.age_ticks < 0 ) {
        mob.age_ticks = glm::min( 0, mob.age_ticks - mob.age_ticks / 10 + 1 );
        // A finishing feed grows the mob right here — the overlap push-out
        // waits for tick_mob, which owns the world access.
        mob.needs_unstick = mob.age_ticks == 0;
        mob.heart_burst = HEART_BABY_FEED_COUNT;
    } else {
        mob.love_ticks = 1;
        mob.heart_ticks = HEART_TICKS;
        mob.ai_state = 0;
        mob.ai_ticks = 0;
    }
    return true;
}

bool MobManager::is_baby_at( const int index ) const {
    return index >= 0 && index < static_cast<int>( this->mobs.size( ) ) && this->mobs[ index ].age_ticks < 0;
}

// Shears on an unshorn adult of a shearable type (MobTypeDef::shear_drop).
// Mirrors answer from the streamed SHEARED/BABY flags — accurate enough to
// let the click through; the owner re-validates on the ENTITY_ACTION.
bool MobManager::shearable( const int index, const ItemID item ) const {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return false;
    }
    const Mob &mob = this->mobs[ index ];
    if ( mob.death_ticks > 0 || mob.uid != 0 ) {
        return false;
    }
    const MobTypeDef &def = mob_types[ mob.type ];
    return item == SHEARS && def.shear_drop.item != LAST_BLOCK_ID && !mob.sheared && mob.age_ticks >= 0;
}

// Applies the shear after shearable() passed: marks the mob sheared +
// touched (a shorn sheep persists like penned livestock) and plays the
// snip with the hurt/feed distance falloff. The caller rolls and spawns
// MobTypeDef::shear_drop where the mob stood.
bool MobManager::shear( const int index, const ItemID item, const glm::dvec3 &sound_pos ) {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return false;
    }
    Mob &mob = this->mobs[ index ];
    if ( mob.net.mirror || !this->shearable( index, item ) ) {
        return false;
    }
    this->mark_touched( mob );
    mob.sheared = 1;
    const double dist = glm::distance( mob.pos, sound_pos );
    const float gain = glm::clamp( 1.0f - static_cast<float>( dist ) / 16.0f, 0.0f, 1.0f );
    audio_play_sfx_gain( "sheep_shear", gain );
    return true;
}

bool MobManager::is_sheared_at( const int index ) const {
    return index >= 0 && index < static_cast<int>( this->mobs.size( ) ) && this->mobs[ index ].sheared != 0;
}

// Called from World::render for every chunk that finished load+finalize this
// frame. Claims ownership (so the leave path knows whether to persist) and
// spawns the worker-side pending_mobs as live mobs.
void MobManager::adopt_chunk( World &world, Chunk &chunk ) {
    // Multiplayer: mob files live server-side; the roll runs on CHUNK_ROLE
    // OWNER instead, so local pending_mobs are discarded.
    if ( !this->enabled || this->test_mode || this->mp_mode ) {
        chunk.pending_mobs.clear( );
        chunk.pending_mobs_removed.clear( );
        return;
    }
    for ( const OwnedChunk &owned : this->owned_chunks ) {
        if ( owned.pos == chunk.chunk_pos ) {
            chunk.pending_mobs.clear( );
            chunk.pending_mobs_removed.clear( );
            return;
        }
    }
    for ( const MobFileRecord &record : chunk.pending_mobs ) {
        // A persisted (touched) mob is restored uncapped — dropping it would
        // lose a mob the save file says we hold. Fresh roll products are
        // population-capped like any other new spawn.
        if ( this->spawn( static_cast<MobType>( record.type ), glm::dvec3( record.x, record.y, record.z ), record.yaw, 1,
                          !( record.flags & NET_MOB_FLAG_TOUCHED ) ) ) {
            Mob &mob = this->mobs.back( );
            mob.variant = record.variant;
            mob.health = record.health;
            if ( net_is_natural_mob_id( record.net_id ) ) {
                // A roll product (fresh or restored): keep its deterministic
                // id so re-rolls and tombstones line up.
                mob.natural_id = record.net_id;
                mob.net_natural = true;
                mob.net_flags |= NET_MOB_FLAG_NATURAL;
            }
            mob.touched = ( record.flags & NET_MOB_FLAG_TOUCHED ) != 0;
            mob.sheared = ( record.flags & NET_MOB_FLAG_SHEARED ) != 0;
            // grow_ticks: negative is a baby's remaining growth, positive
            // an adult's re-breed cooldown (older files zero-fill both).
            if ( record.grow_ticks < 0 ) {
                mob.age_ticks = record.grow_ticks;
            } else {
                mob.breed_cooldown = record.grow_ticks;
            }
        }
    }
    this->owned_chunks.push_back( { chunk.chunk_pos, chunk.mobs_file_present,
                                    std::set<uint8_t>( chunk.pending_mobs_removed.begin( ), chunk.pending_mobs_removed.end( ) ) } );
    chunk.pending_mobs.clear( );
    chunk.pending_mobs_removed.clear( );
}

// Roughly every 16 ticks: a chunk has left when its slot is empty, carries a
// different position, or is PARKED (deferred release already claimed it).
// CHUNK_LOADING at the same pos has not left — the slot is being reused for
// the same position.
void MobManager::poll_owned( World &world ) {
    if ( this->mp_mode ) {
        return; // no local mobs_* persistence in multiplayer
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( size_t i = 0; i < this->owned_chunks.size( ); ) {
        const OwnedChunk &owned = this->owned_chunks[ i ];
        const Chunk *chunk = world.chunkLoader.get_chunk( owned.pos );
        const bool left = chunk == nullptr || chunk->chunk_pos != owned.pos || chunk->load_state == CHUNK_PARKED;
        if ( !left ) {
            i++;
            continue;
        }
        std::vector<MobFileRecord> records;
        for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
            const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( it->pos / csize ) );
            if ( it->persistent && !it->despawnable && feet_chunk == owned.pos ) {
                // A pristine natural is dropped rather than persisted — the
                // deterministic roll recreates it on the next load.
                if ( it->natural_id != 0 && !it->touched ) {
                    it = this->remove_mob( it );
                    continue;
                }
                MobFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u, 0u, 0 };
                record.type = static_cast<uint16_t>( it->type );
                record.variant = static_cast<uint16_t>( it->variant < 0 ? 0 : it->variant );
                record.x = it->pos.x;
                record.y = it->pos.y;
                record.z = it->pos.z;
                record.yaw = it->yaw;
                record.health = it->health;
                record.flags = mob_net_flags( *it ) & ( NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED | NET_MOB_FLAG_SHEARED );
                record.net_id = it->natural_id;
                record.grow_ticks = it->age_ticks < 0 ? it->age_ticks : it->breed_cooldown;
                records.push_back( record );
                it = this->remove_mob( it );
            } else {
                ++it;
            }
        }
        // Write only if there is something worth recording: live mobs, a
        // file we loaded, or tombstoned roll indices. An owned chunk with
        // only pristine naturals leaves no file behind — and persist_mobs
        // deletes a stale file when both lists are empty.
        // No writes in capture or fly mode — capture exits without player.dat
        // and fly is a throwaway scripted run; both must leave no mob files.
        if ( this->storage != nullptr && !RepGame::capture_mode( ) && !RepGame::fly_mode( ) &&
             ( !records.empty( ) || !owned.removed.empty( ) || owned.file_present ) ) {
            const std::vector<uint8_t> removed( owned.removed.begin( ), owned.removed.end( ) );
            this->storage->persist_mobs( owned.pos, records, removed );
        }
        this->owned_chunks.erase( this->owned_chunks.begin( ) + static_cast<long>( i ) );
    }
}

void MobManager::persist_all( ) {
    if ( this->storage == nullptr || RepGame::fly_mode( ) || RepGame::capture_mode( ) || this->mp_mode ) {
        return;
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( const OwnedChunk &owned : this->owned_chunks ) {
        std::vector<MobFileRecord> records;
        for ( const Mob &mob : this->mobs ) {
            // Pristine naturals are recreated by the roll — only touched
            // naturals and other persistent mobs become records.
            const bool pristine = mob.natural_id != 0 && !mob.touched;
            if ( mob.persistent && !mob.despawnable && !pristine && glm::ivec3( glm::floor( mob.pos / csize ) ) == owned.pos ) {
                MobFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u, 0u, 0 };
                record.type = static_cast<uint16_t>( mob.type );
                record.variant = static_cast<uint16_t>( mob.variant < 0 ? 0 : mob.variant );
                record.x = mob.pos.x;
                record.y = mob.pos.y;
                record.z = mob.pos.z;
                record.yaw = mob.yaw;
                record.health = mob.health;
                record.flags = mob_net_flags( mob ) & ( NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED | NET_MOB_FLAG_SHEARED );
                record.net_id = mob.natural_id;
                record.grow_ticks = mob.age_ticks < 0 ? mob.age_ticks : mob.breed_cooldown;
                records.push_back( record );
            }
        }
        if ( !records.empty( ) || !owned.removed.empty( ) || owned.file_present ) {
            const std::vector<uint8_t> removed( owned.removed.begin( ), owned.removed.end( ) );
            this->storage->persist_mobs( owned.pos, records, removed );
        }
    }
}

void MobManager::tick( World &world, const glm::dvec3 &player_pos, const MobTickInput *in, MobTickOutput *out ) {
    PROFILE_SCOPE( "mobs_tick" );
    // Remote mobs tick in every mode (they're driven by the network, not the
    // world); everything else freezes under REPGAME_MOBS=0 and capture mode.
    const bool freeze = !this->enabled || RepGame::capture_mode( );
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const float player_eye = in != nullptr ? in->player_eye_height : PLAYER_EYE_HEIGHT;
    const glm::dvec3 player_feet = player_pos - glm::dvec3( 0.0, player_eye, 0.0 );
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
        // The feed-heart window runs down for local and mirror mobs alike
        // (mirrors get it from the NET_MOB_FLAG_LOVE edge, not feed()).
        if ( it->heart_ticks > 0 ) {
            it->heart_ticks--;
        }
        if ( it->uid != 0 ) {
            this->tick_remote( *it );
            ++it;
            continue;
        }
        if ( it->net.mirror ) {
            // Mirror of a remote-owned mob: interpolation + flag-driven
            // visuals only, no physics/AI/despawn. ENTITY_REMOVE ends it.
            this->tick_mirror( *it, player_pos );
            ++it;
            continue;
        }
        // Night-spawner mobs vanish instead of freezing or lingering: past 64
        // blocks outright, a slow random roll past 40, or when their feet
        // chunk leaves the live window. In multiplayer the distance is to
        // the nearest player (local or remote).
        if ( it->despawnable ) {
            double dist = glm::distance( it->pos, player_feet );
            if ( in != nullptr ) {
                for ( const MobRemoteTarget &remote : in->remote_targets ) {
                    const glm::dvec3 feet = remote.eye_pos - glm::dvec3( 0.0, remote.eye_height, 0.0 );
                    dist = fmin( dist, glm::distance( it->pos, feet ) );
                }
            }
            std::uniform_real_distribution<double> roll( 0.0, 1.0 );
            if ( dist > 64.0 || ( dist > 40.0 && roll( this->rng ) < 1.0 / ( 8.0 * UPS_RATE ) ) ) {
                this->send_net_remove( *it, NET_REMOVE_DESPAWN );
                it = this->remove_mob( it );
                continue;
            }
        }
        if ( freeze ) {
            it->prev_pos = it->pos;
            it->prev_yaw = it->yaw;
            ++it;
            continue;
        }
        // A mob whose feet chunk isn't live is frozen: parked/released slots
        // report LAST_BLOCK_ID from get_loaded_block and a LOADING slot at the
        // same position hasn't finished adopting back yet. Despawnable mobs
        // are removed instead of frozen.
        const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( it->pos / csize ) );
        const Chunk *chunk = world.chunkLoader.get_chunk( feet_chunk );
        if ( chunk == nullptr || chunk->load_state != CHUNK_LIVE ) {
            if ( it->despawnable ) {
                this->send_net_remove( *it, NET_REMOVE_DESPAWN );
                it = this->remove_mob( it );
                continue;
            }
            it->prev_pos = it->pos;
            it->prev_yaw = it->yaw;
            ++it;
            continue;
        }
        // Pen rule: an owned natural mob standing in a player-edited chunk
        // (fences, pens) counts as interacted — it becomes persistable and
        // its roll index is tombstoned.
        if ( it->natural_id != 0 && !it->touched && chunk->player_edited ) {
            this->mark_touched( *it );
        }
        if ( it->death_ticks > 0 && it->death_ticks > static_cast<int>( 0.5f * UPS_RATE ) ) {
            this->send_net_remove( *it, NET_REMOVE_DIED );
            it = this->remove_mob( it );
            continue;
        }
        this->tick_mob( world, *it, mob_types[ it->type ], player_pos, in, out );
        // A creeper's finished fuse explodes immediately — no tip-over, no
        // drop; RepGame turns the position into a TntExplodeEvent.
        if ( it->fuse_ticks > 0 && it->fuse_ticks >= static_cast<int>( 1.5f * UPS_RATE ) ) {
            if ( out != nullptr ) {
                out->explosions.push_back( it->pos );
            }
            this->send_net_remove( *it, NET_REMOVE_EXPLODED );
            it = this->remove_mob( it );
            continue;
        }
        ++it;
    }
    // Mating: a same-type pair both in love produces a baby when their
    // hitboxes meet. Runs after the per-mob pass so the spawn never
    // invalidates the iteration; the parents' love clears and a 2 min
    // cooldown starts (love_ticks==0 also short-circuits later pairs).
    if ( !freeze ) {
        for ( size_t i = 0; i < this->mobs.size( ); i++ ) {
            Mob &a = this->mobs[ i ];
            if ( a.love_ticks <= 0 || a.uid != 0 || a.net.mirror ) {
                continue;
            }
            for ( size_t j = i + 1; j < this->mobs.size( ); j++ ) {
                Mob &b = this->mobs[ j ];
                if ( b.type != a.type || b.love_ticks <= 0 || b.uid != 0 || b.net.mirror ) {
                    continue;
                }
                const MobTypeDef &def = mob_types[ a.type ];
                const double dxz = std::hypot( b.pos.x - a.pos.x, b.pos.z - a.pos.z );
                if ( dxz > def.hitbox_w + 0.2 || fabs( b.pos.y - a.pos.y ) > 1.5 ) {
                    continue;
                }
                a.love_ticks = 0;
                b.love_ticks = 0;
                a.breed_cooldown = MOB_BREED_COOLDOWN_TICKS;
                b.breed_cooldown = MOB_BREED_COOLDOWN_TICKS;
                // The baby is a player-caused spawn (uncapped, persistent,
                // touched) — it never rolls back from the natural table.
                if ( this->spawn_any( a.type, ( a.pos + b.pos ) * 0.5, a.yaw, 1 ) ) {
                    Mob &baby = this->mobs.back( );
                    baby.age_ticks = -MOB_BABY_AGE_TICKS;
                    baby.touched = true;
                    baby.heart_burst = HEART_BORN_COUNT;
                    this->announce_spawn( baby );
                }
                break;
            }
        }
    }
    // Owned-chunk leave-poll (~every 16 ticks).
    if ( ++this->poll_count >= 16 ) {
        this->poll_count = 0;
        this->poll_owned( world );
    }
    // Night spawner: only a live game tick with a targetable-player input.
    if ( in != nullptr && this->enabled && this->spawner_enabled && !this->test_mode && !RepGame::capture_mode( ) && !RepGame::fly_mode( ) ) {
        this->tick_spawner( world, player_feet, *in );
    }
}

// Every 2 s: while fewer than 12 night-spawned hostiles are alive, try up to
// 4 times to place one on a dark surface 24-44 blocks from the player.
void MobManager::tick_spawner( World &world, const glm::dvec3 &player_feet, const MobTickInput &in ) {
    if ( ++this->spawn_ticks < 2 * UPS_RATE ) {
        return;
    }
    this->spawn_ticks = 0;
    if ( !this->under_mob_cap( ) ) {
        return;
    }
    int live = 0;
    for ( const Mob &mob : this->mobs ) {
        // In multiplayer only despawnables in chunks we own count: mirrors
        // and other clients' owned mobs aren't ours to budget.
        live += mob.despawnable && ( this->net == nullptr || this->net->owns_chunk( mob.net.chunk ) );
    }
    if ( live >= 12 ) {
        return;
    }
    const float daylight = light_daylight_factor( in.world_time );
    const int player_y = static_cast<int>( floor( player_feet.y ) );
    std::uniform_real_distribution<double> angle_pick( 0.0, 2.0 * M_PI );
    std::uniform_real_distribution<double> dist_pick( 24.0, 44.0 );
    std::uniform_real_distribution<float> yaw_pick( 0.0f, 360.0f );
    std::uniform_real_distribution<double> type_roll( 0.0, 1.0 );
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( int attempt = 0; attempt < 4; attempt++ ) {
        const double angle = angle_pick( this->rng );
        const double dist = dist_pick( this->rng );
        const double sx = player_feet.x + sin( angle ) * dist;
        const double sz = player_feet.z + cos( angle ) * dist;
        const int bx = static_cast<int>( floor( sx ) );
        const int bz = static_cast<int>( floor( sz ) );
        int top = -1;
        for ( int y = player_y + 16; y > player_y + 16 - 40; y-- ) {
            if ( solid_at( world, glm::ivec3( bx, y, bz ) ) ) {
                top = y;
                break;
            }
        }
        // Needs a floor with two non-colliding cells above for the mob.
        if ( top < 0 || solid_at( world, glm::ivec3( bx, top + 1, bz ) ) || solid_at( world, glm::ivec3( bx, top + 2, bz ) ) ) {
            continue;
        }
        const glm::dvec3 feet_pos( sx, top + 1.0, sz );
        const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( feet_pos / csize ) );
        const Chunk *chunk = world.chunkLoader.get_chunk( feet_chunk );
        if ( chunk == nullptr || chunk->load_state != CHUNK_LIVE ) {
            continue;
        }
        // Multiplayer: the night spawner only places into chunks we own.
        if ( this->mp_mode && ( this->net == nullptr || !this->net->owns_chunk( feet_chunk ) ) ) {
            continue;
        }
        const glm::vec2 light = world.chunkLoader.light_sample( glm::vec3( feet_pos ) + glm::vec3( 0.0f, 0.5f, 0.0f ) );
        if ( !hostile_spawn_light_ok( light.x, light.y, daylight ) ) {
            continue;
        }
        const MobType type = type_roll( this->rng ) < 0.6 ? MOB_ZOMBIE : MOB_CREEPER;
        if ( this->spawn_any( type, feet_pos, yaw_pick( this->rng ), 0 ) ) {
            Mob &mob = this->mobs.back( );
            mob.despawnable = 1;
            this->announce_spawn( mob );
            live++;
            if ( live >= 12 ) {
                return;
            }
        }
    }
}

// Bone rest + animator rotation, in degrees, plus an animator position
// offset in model pixels. The offset translates the whole bone (pivot and
// children) in the parent frame.
static glm::mat4 bone_local_matrix( const MobBoneDef &bone, const glm::vec3 &anim_rot, const glm::vec3 &anim_offset ) {
    const glm::vec3 pivot( bone.pivot[ 0 ], bone.pivot[ 1 ], bone.pivot[ 2 ] );
    const glm::vec3 rot( bone.rest_rot[ 0 ] + anim_rot.x, bone.rest_rot[ 1 ] + anim_rot.y, bone.rest_rot[ 2 ] + anim_rot.z );
    glm::mat4 m = glm::translate( glm::mat4( 1.0f ), pivot );
    m = glm::rotate( m, glm::radians( rot.x ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    m = glm::rotate( m, glm::radians( rot.y ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    m = glm::rotate( m, glm::radians( rot.z ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
    m = glm::translate( m, -pivot );
    return glm::translate( glm::mat4( 1.0f ), anim_offset ) * m;
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
    for ( int i = 0; i < bone_count; i++ ) {
        const MobBoneDef &bone = model.bones[ i ];
        if ( ( def.anims & MOB_ANIM_HEAD_LOOK ) && def.bone_head != NULL && strcmp( bone.name, def.bone_head ) == 0 ) {
            bone_anim[ i ] = mob_head_anim( mob.head_yaw, mob.head_pitch );
        }
        if ( def.anims & MOB_ANIM_QUADRUPED_WALK ) {
            for ( int leg = 0; leg < 4; leg++ ) {
                if ( def.bone_legs[ leg ] != NULL && strcmp( bone.name, def.bone_legs[ leg ] ) == 0 ) {
                    const float sign = ( leg == 0 || leg == 3 ) ? 1.0f : -1.0f;
                    bone_anim[ i ].x += sign * 80.0f * cosf( phase ) * mob.leg_amp;
                }
            }
        }
        if ( def.anims & MOB_ANIM_WING_FLAP ) {
            // Bedrock animation.chicken.general: wing0 flaps +z, wing1 -z.
            // 20° on the ground, 70° while airborne (gliding).
            const float amp = mob.standing ? 20.0f : 70.0f;
            for ( int w = 0; w < 2; w++ ) {
                if ( def.bone_wings[ w ] != NULL && strcmp( bone.name, def.bone_wings[ w ] ) == 0 ) {
                    bone_anim[ i ].z += ( w == 0 ? 1.0f : -1.0f ) * amp * sinf( mob.wing_phase );
                }
            }
        }
        if ( def.anims & MOB_ANIM_HUMANOID_WALK ) {
            // Bedrock animation.humanoid.move: tcos0 = cos(phase)*57.3°,
            // opposite arm and leg swing together (right leg + left arm).
            const float tcos0 = cosf( phase ) * 57.3f * mob.leg_amp;
            for ( int limb = 0; limb < 2; limb++ ) {
                if ( def.bone_legs[ limb ] != NULL && strcmp( bone.name, def.bone_legs[ limb ] ) == 0 ) {
                    bone_anim[ i ].x += ( limb == 0 ? 1.4f : -1.4f ) * tcos0;
                }
                if ( def.bone_arms[ limb ] != NULL && strcmp( bone.name, def.bone_arms[ limb ] ) == 0 ) {
                    bone_anim[ i ].x += ( limb == 0 ? -1.0f : 1.0f ) * tcos0;
                }
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
        const glm::mat4 local = bone_local_matrix( bone, bone_anim[ i ], bone_offset[ i ] );
        out[ i ] = bone.parent >= 0 ? out[ bone.parent ] * local : local;
    }

    if ( ( def.anims & MOB_ANIM_HUMANOID_SNEAK ) && s > 0.0f ) {
        glm::mat4 s_eff[ 32 ];
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            float angle = 0.0f;
            glm::vec3 off( 0.0f );
            int named = 0;
            if ( def.bone_body != NULL && strcmp( bone.name, def.bone_body ) == 0 ) {
                angle = -28.65f;
                off.y = -1.5f;
                named = 1;
            } else if ( def.bone_head != NULL && strcmp( bone.name, def.bone_head ) == 0 ) {
                off.y = -2.5f;
                named = 1;
            } else {
                for ( int limb = 0; limb < 2; limb++ ) {
                    if ( def.bone_arms[ limb ] != NULL && strcmp( bone.name, def.bone_arms[ limb ] ) == 0 ) {
                        angle = -22.9f;
                        off.y = -1.5f;
                        named = 1;
                    }
                    if ( def.bone_legs[ limb ] != NULL && strcmp( bone.name, def.bone_legs[ limb ] ) == 0 ) {
                        off.z = 4.0f;
                        named = 1;
                    }
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
    if ( !this->render_inited ) {
        return;
    }
    for ( Mob &mob : this->mobs ) {
        const MobTypeDef &def = mob_types[ mob.type ];
        // A shorn shearable type renders its sheared model (a smaller cube
        // set sharing the bone names); spare parts allocated for the larger
        // model get a zeroed transform below.
        const MobModelDef &model =
            mob.sheared && def.sheared_model != MOB_MODEL_COUNT ? mob_models[ def.sheared_model ] : mob_models[ def.model ];

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
        base = glm::scale( base, glm::vec3( scale_of( mob ) / 16.0f ) );

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
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            for ( int c = 0; c < bone.cube_count; c++ ) {
                const MobCubeDef &cube = model.cubes[ bone.cube_start + c ];
                const glm::vec3 origin( cube.origin[ 0 ] - cube.inflate, cube.origin[ 1 ] - cube.inflate, cube.origin[ 2 ] - cube.inflate );
                const glm::vec3 size( cube.size[ 0 ] + 2.0f * cube.inflate, cube.size[ 1 ] + 2.0f * cube.inflate, cube.size[ 2 ] + 2.0f * cube.inflate );
                const glm::mat4 cube_m = glm::scale( glm::translate( glm::mat4( 1.0f ), origin ), size );
                MobPartInstance &inst = this->render_chain.get_instance( mob.parts[ part++ ] );
                inst.transform = base * bone_world[ i ] * cube_m;
                inst.light = light;
                inst.uv_a = glm::vec4( cube.uv[ 0 ], cube.uv[ 1 ], cube.size[ 0 ], cube.size[ 1 ] );
                // uv_b.w < 0 is the red hurt flash; > 0 is the creeper's
                // white fuse flash on alternate tenths. Hurt wins.
                const bool white_flash = fuse_f > 0.0f && static_cast<int>( fuse_f * 10.0f ) % 2 == 0;
                inst.uv_b = glm::vec4( cube.size[ 2 ], static_cast<float>( def.skins[ mob.variant ] ),
                                       ( bone.mirror || cube.mirror ) ? 1.0f : 0.0f, mob.hurt_ticks > 0 ? -0.7f : white_flash ? 0.5f : 0.0f );
            }
        }
        // Parts allocated beyond the active model's cubes (the woolly model
        // is bigger than the sheared one) render nothing.
        while ( part < static_cast<int>( mob.parts.size( ) ) ) {
            MobPartInstance &inst = this->render_chain.get_instance( mob.parts[ part++ ] );
            inst.transform = glm::mat4( 0.0f );
        }
    }
    this->render_chain.invalidate_all( );
}

void MobManager::draw( const Renderer &renderer, const Shader &shader ) {
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

void MobManager::cleanup( ) {
    this->render_chain.clear( );
    this->mobs.clear( );
}

// ---------------------------------------------------------------------------
// Multiplayer sync (server arbitrates, owner client simulates). Same model
// as ItemEntities: net_chunk is the chunk the server believes the mob sits
// in; all ownership checks key off it so a crossing still streams until
// ENTITY_TRANSFER arrives. All paths are no-ops in single player.
// ---------------------------------------------------------------------------

static glm::ivec3 mob_net_chunk_of( const glm::dvec3 &pos ) {
    return net_entity_chunk_of( pos );
}

uint32_t MobManager::mob_net_flags( const Mob &mob ) {
    uint32_t f = mob.net_flags & ( NET_MOB_FLAG_DESPAWNABLE | NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED );
    if ( mob.despawnable ) {
        f |= NET_MOB_FLAG_DESPAWNABLE;
    }
    if ( mob.net_natural ) {
        f |= NET_MOB_FLAG_NATURAL;
    }
    if ( mob.touched ) {
        f |= NET_MOB_FLAG_TOUCHED;
    }
    if ( mob.hurt_ticks > 0 ) {
        f |= NET_MOB_FLAG_HURT;
    }
    if ( mob.death_ticks > 0 ) {
        f |= NET_MOB_FLAG_DYING;
    }
    if ( mob.fuse_ticks > 0 ) {
        f |= NET_MOB_FLAG_FUSE;
    }
    if ( mob.love_ticks > 0 ) {
        f |= NET_MOB_FLAG_LOVE;
    }
    if ( mob.age_ticks < 0 ) {
        f |= NET_MOB_FLAG_BABY;
    }
    if ( mob.sheared ) {
        f |= NET_MOB_FLAG_SHEARED;
    }
    return f;
}

void MobManager::set_multiplayer_mode( const bool mp_mode, NetEntities *net ) {
    this->mp_mode = mp_mode;
    this->net = mp_mode ? net : nullptr;
}

void MobManager::announce_spawn( Mob &mob ) {
    if ( this->net == nullptr || mob.net.id != 0 || mob.uid != 0 ) {
        return;
    }
    const NetEntityId nid = this->net->mint_id( );
    if ( nid == 0 ) {
        return;
    }
    mob.net.id = nid;
    mob.net.chunk = mob_net_chunk_of( mob.pos );
    NetEntityRecord rec;
    rec.id = nid;
    rec.kind = NetEntityKind::MOB;
    rec.x = mob.pos.x;
    rec.y = mob.pos.y;
    rec.z = mob.pos.z;
    rec.vx = static_cast<float>( mob.vel.x );
    rec.vy = static_cast<float>( mob.vel.y );
    rec.vz = static_cast<float>( mob.vel.z );
    rec.yaw = mob.yaw;
    rec.type = mob.type;
    rec.count = mob.variant;
    rec.health = mob.health;
    rec.age_ticks = mob.age_ticks < 0 ? mob.age_ticks : mob.breed_cooldown;
    rec.flags = mob_net_flags( mob );
    this->net->send_spawn( rec );
}

void MobManager::send_net_remove( Mob &mob, const uint8_t reason ) {
    netsync_send_remove_owned( this->net, mob.net, reason );
}

MobManager::Mob *MobManager::find_by_net_id( const NetEntityId net_id ) {
    return netsync_find( this->mobs, net_id );
}

// Mirror of a remote-owned mob: no physics/AI/spawner/despawn — ease pos and
// yaw toward the last streamed targets, derive the gait from the applied
// horizontal motion, and let the flag-driven visuals (hurt flash, tip-over,
// creeper swell) play out. Ambient say/step sounds still run locally.
void MobManager::tick_mirror( Mob &mob, const glm::dvec3 &player_pos ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    mob.prev_sneak = mob.sneak;
    const glm::dvec3 delta = mob.net.pos - mob.pos;
    if ( glm::length( delta ) > 4.0 ) {
        mob.pos = mob.net.pos;
    } else {
        mob.pos += delta * 0.35;
    }
    mob.yaw += wrap_degrees_180( mob.net_yaw - mob.yaw ) * 0.35f;
    mob.vel = mob.net.vel;
    const glm::dvec3 moved = mob.pos - mob.prev_pos;
    const double hd = std::hypot( moved.x, moved.z );
    mob.walk_dist += static_cast<float>( hd );
    const float amp_target = static_cast<float>( fmin( hd * ( UPS_RATE / 20.0 ) * 4.0, 1.0 ) );
    mob.leg_amp += ( amp_target - mob.leg_amp ) * 0.2f;
    mob.wing_phase += 0.06f;
    if ( mob.hurt_ticks > 0 ) {
        mob.hurt_ticks--;
    }
    if ( mob.death_ticks > 0 ) {
        // The tip-over plays through but nothing removes the mob locally —
        // only ENTITY_REMOVE does.
        mob.death_ticks++;
    }
    if ( mob.fuse_ticks > 0 ) {
        mob.fuse_ticks++; // swell progress; cleared when the FUSE flag drops
    }

    const MobTypeDef &def = mob_types[ mob.type ];
    const double dist = glm::distance( mob.pos, player_pos );
    if ( def.sound_say != NULL && --mob.say_ticks <= 0 ) {
        std::uniform_int_distribution<int> say( 10 * UPS_RATE, 30 * UPS_RATE );
        mob.say_ticks = say( this->rng );
        if ( dist < 16.0 ) {
            audio_play_sfx_gain( def.sound_say, 1.0f - static_cast<float>( dist ) / 16.0f );
        }
    }
    mob.step_acc += static_cast<float>( hd );
    if ( def.sound_step != NULL && mob.step_acc >= 0.7f && dist < 8.0 ) {
        mob.step_acc = 0.0f;
        audio_play_sfx_gain( def.sound_step, def.step_gain * ( 1.0f - static_cast<float>( dist ) / 16.0f ) );
    }
}

bool MobManager::spawn_record( const NetEntityRecord &rec, const bool mirror ) {
    if ( !this->enabled || rec.type >= MOB_TYPE_COUNT ) {
        return false;
    }
    if ( mob_types[ rec.type ].ai == MOB_AI_REMOTE ) {
        return false; // remote players come through spawn_remote, never this path
    }
    if ( !this->spawn_any( static_cast<MobType>( rec.type ), glm::dvec3( rec.x, rec.y, rec.z ), rec.yaw, 1 ) ) {
        return false;
    }
    Mob &mob = this->mobs.back( );
    netsync_apply_record( mob.net, rec, mirror );
    mob.net_yaw = rec.yaw;
    mob.variant = rec.count;
    mob.health = rec.health;
    mob.net_flags = rec.flags;
    mob.despawnable = ( rec.flags & NET_MOB_FLAG_DESPAWNABLE ) ? 1 : 0;
    mob.net_natural = ( rec.flags & NET_MOB_FLAG_NATURAL ) != 0;
    mob.natural_id = net_is_natural_mob_id( rec.id ) ? rec.id : 0;
    mob.touched = ( rec.flags & NET_MOB_FLAG_TOUCHED ) != 0;
    mob.sheared = ( rec.flags & NET_MOB_FLAG_SHEARED ) ? 1 : 0;
    // MOB records carry breed state in age_ticks, but state entries can't
    // refresh it — a value negative at spawn outlives a grow-up server-side.
    // The live BABY flag arbitrates the sign: negative growth ticks while
    // flagged, else a positive re-breed cooldown.
    if ( rec.flags & NET_MOB_FLAG_BABY ) {
        mob.age_ticks = std::min<int32_t>( rec.age_ticks, -1 );
        mob.heart_burst = HEART_BORN_COUNT;
    } else {
        mob.breed_cooldown = std::max<int32_t>( rec.age_ticks, 0 );
        mob.age_ticks = 0;
    }
    // A mob spawning into view already in love opens its heart window.
    if ( rec.flags & NET_MOB_FLAG_LOVE ) {
        mob.heart_ticks = HEART_TICKS;
    }
    if ( rec.flags & NET_MOB_FLAG_HURT ) {
        mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
    }
    if ( rec.flags & NET_MOB_FLAG_DYING ) {
        mob.death_ticks = 1;
    }
    if ( rec.flags & NET_MOB_FLAG_FUSE ) {
        mob.fuse_ticks = 1;
    }
    return true;
}

void MobManager::net_apply_spawn( const NetEntityRecord &rec, const bool mirror ) {
    if ( !this->enabled ) {
        return;
    }
    Mob *existing = this->find_by_net_id( rec.id );
    if ( existing != nullptr ) {
        existing->net.mirror = mirror;
        existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
        existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        existing->net_yaw = rec.yaw;
        existing->net.chunk = mob_net_chunk_of( existing->net.pos );
        return;
    }
    this->spawn_record( rec, mirror );
}

void MobManager::net_apply_state( const NetEntityStateEntry &e ) {
    if ( !this->enabled ) {
        return;
    }
    Mob *mob = this->find_by_net_id( e.id );
    if ( mob == nullptr ) {
        return;
    }
    // Track the chunk the server's copy sits in (same boundary hysteresis
    // the server applies); a predicted local spawn we don't own flips to a
    // mirror on its first authoritative state.
    netsync_track_chunk( mob->net, glm::dvec3( e.x, e.y, e.z ) );
    const bool owned_here = this->net != nullptr && this->net->owns_chunk( mob->net.chunk );
    if ( !mob->net.mirror && owned_here ) {
        // Owned: no server-originated mob state (partial pickups are items
        // only); just keep the provenance flag bits in sync.
        mob->net_flags = ( mob->net_flags & ~( NET_MOB_FLAG_DESPAWNABLE | NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED ) ) |
                         ( e.flags & ( NET_MOB_FLAG_DESPAWNABLE | NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED ) );
        return;
    }
    const uint32_t old_flags = mob->net_flags;
    mob->net.mirror = true;
    mob->net.pos = glm::dvec3( e.x, e.y, e.z );
    mob->net.vel = glm::dvec3( e.vx, e.vy, e.vz );
    mob->net_yaw = e.yaw;
    mob->variant = e.count;
    mob->health = e.health;
    mob->net_flags = e.flags;
    mob->despawnable = ( e.flags & NET_MOB_FLAG_DESPAWNABLE ) ? 1 : 0;
    mob->touched = ( e.flags & NET_MOB_FLAG_TOUCHED ) != 0;
    mob->sheared = ( e.flags & NET_MOB_FLAG_SHEARED ) ? 1 : 0;
    // BABY is the only age signal state entries carry: a mirror keeps its
    // own growth countdown while flagged, and snaps adult on the clear.
    if ( e.flags & NET_MOB_FLAG_BABY ) {
        if ( mob->age_ticks >= 0 ) {
            mob->age_ticks = -1;
        }
    } else {
        mob->age_ticks = 0;
    }
    const MobTypeDef &def = mob_types[ mob->type ];
    // Rising-edge flag visuals: the owner's sim already played its sfx, so
    // mirror playback is full-gain (no attacker distance to fade against).
    if ( ( e.flags & NET_MOB_FLAG_HURT ) && !( old_flags & NET_MOB_FLAG_HURT ) ) {
        mob->hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
        if ( def.sound_hurt != NULL ) {
            audio_play_sfx( def.sound_hurt );
        }
    }
    // LOVE rides the same edge: the owner's feed() opens its own heart
    // window, so this opens it on every other client.
    if ( ( e.flags & NET_MOB_FLAG_LOVE ) && !( old_flags & NET_MOB_FLAG_LOVE ) ) {
        mob->heart_ticks = HEART_TICKS;
    }
    // The snip is the SHEARED edge's sound — the owner's shear() already
    // played it for the shearer, mirrors play it here at full gain.
    if ( ( e.flags & NET_MOB_FLAG_SHEARED ) && !( old_flags & NET_MOB_FLAG_SHEARED ) ) {
        audio_play_sfx( "sheep_shear" );
    }
    if ( ( e.flags & NET_MOB_FLAG_DYING ) && !( old_flags & NET_MOB_FLAG_DYING ) ) {
        mob->death_ticks = 1;
        if ( def.sound_death != NULL ) {
            audio_play_sfx( def.sound_death );
        }
    }
    if ( e.flags & NET_MOB_FLAG_FUSE ) {
        if ( !( old_flags & NET_MOB_FLAG_FUSE ) ) {
            mob->fuse_ticks = 1;
            audio_play_sfx( "fuse" );
        }
    } else {
        mob->fuse_ticks = 0;
    }
}

void MobManager::net_apply_remove( const NetEntityId net_id ) {
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ++it ) {
        if ( it->net.id == net_id && net_id != 0 ) {
            this->remove_mob( it );
            return;
        }
    }
}

void MobManager::net_release_entity( const NetEntityRecord &rec, const bool still_viewed ) {
    Mob *mob = this->find_by_net_id( rec.id );
    if ( mob == nullptr ) {
        return;
    }
    if ( still_viewed ) {
        mob->net.mirror = true;
        mob->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
        mob->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        mob->net_yaw = rec.yaw;
        mob->net.chunk = mob_net_chunk_of( mob->net.pos );
    } else {
        for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ++it ) {
            if ( &*it == mob ) {
                this->remove_mob( it );
                return;
            }
        }
    }
}

void MobManager::net_apply_action( RepGameState &state, const NetEntityActionPayload &p ) {
    if ( !this->enabled || ( p.action != NET_ACTION_HIT && p.action != NET_ACTION_FEED && p.action != NET_ACTION_SHEAR ) ) {
        return;
    }
    Mob *mob = this->find_by_net_id( p.id );
    if ( mob == nullptr || mob->net.mirror || this->net == nullptr || !this->net->owns_chunk( mob->net.chunk ) ) {
        return;
    }
    const int index = static_cast<int>( mob - this->mobs.data( ) );
    if ( p.action == NET_ACTION_FEED ) {
        // The item is what the sender claims to hold — feedable() validates
        // it against the type's breed items before applying.
        const bool fed = this->feed( index, static_cast<ItemID>( p.item ) );
        mp_test::event( &state, "mob_action_feed", "\"id\":\"%llu\",\"item\":%u,\"fed\":%d",
                        (unsigned long long)p.id, p.item, (int)fed );
        return;
    }
    if ( p.action == NET_ACTION_SHEAR ) {
        // Same claimed-item validation: shearable() wants real shears.
        const bool sheared = this->shear( index, static_cast<ItemID>( p.item ), glm::dvec3( p.ax, p.ay, p.az ) );
        mp_test::event( &state, "mob_action_shear", "\"id\":\"%llu\",\"item\":%u,\"sheared\":%d",
                        (unsigned long long)p.id, p.item, (int)sheared );
        if ( sheared ) {
            RepGame::spawn_mob_shear_drop( state, mob_types[ this->mobs[ index ].type ], this->mobs[ index ].pos );
        }
        return;
    }
    const bool killed = this->hurt( index, p.damage, glm::dvec3( p.ax, p.ay, p.az ) );
    mp_test::event( &state, "mob_action_hit", "\"id\":\"%llu\",\"dmg\":%g,\"killed\":%d,\"survival\":%d",
                    (unsigned long long)p.id, p.damage, (int)killed, (int)p.attacker_survival );
    if ( killed && p.attacker_survival && !this->is_baby_at( index ) ) {
        RepGame::spawn_mob_drops( state, mob_types[ this->mobs[ index ].type ], this->mobs[ index ].pos );
    }
}

void MobManager::mirror_hit_feedback( const int index, const glm::dvec3 &attacker_pos ) {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return;
    }
    Mob &mob = this->mobs[ index ];
    const MobTypeDef &def = mob_types[ mob.type ];
    mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
    const double dist = glm::distance( mob.pos, attacker_pos );
    const float gain = glm::clamp( 1.0f - static_cast<float>( dist ) / 16.0f, 0.0f, 1.0f );
    if ( def.sound_hurt != NULL ) {
        audio_play_sfx_gain( def.sound_hurt, gain );
    }
}

void MobManager::net_apply_chunk_role( RepGameState &state, const glm::ivec3 &chunk, const uint8_t role, const uint8_t populated,
                                       const std::vector<NetEntityRecord> &records, const std::vector<uint8_t> &removed_natural,
                                       const std::set<NetEntityId> &keep_ids ) {
    if ( !this->enabled ) {
        return;
    }
    if ( role == NET_ROLE_NONE ) {
        for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
            if ( it->net.id != 0 && it->net.chunk == chunk ) {
                it = this->remove_mob( it );
            } else {
                ++it;
            }
        }
        return;
    }
    const bool owned = role == NET_ROLE_OWNER;
    std::unordered_map<NetEntityId, const NetEntityRecord *> by_id;
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind == NetEntityKind::MOB ) {
            by_id[ rec.id ] = &rec;
        }
    }
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
        if ( it->net.id != 0 && it->net.chunk == chunk && by_id.find( it->net.id ) == by_id.end( ) && !keep_ids.count( it->net.id ) ) {
            it = this->remove_mob( it );
        } else {
            ++it;
        }
    }
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind != NetEntityKind::MOB ) {
            continue;
        }
        Mob *existing = this->find_by_net_id( rec.id );
        if ( existing != nullptr ) {
            existing->net.mirror = !owned;
            existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
            existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
            existing->net_yaw = rec.yaw;
            // The grant's chunk is authoritative — a record resting in the
            // boundary margin has floor(pos) != the server's chunk.
            existing->net.chunk = chunk;
            existing->variant = rec.count;
            existing->health = rec.health;
            existing->net_flags = rec.flags;
            existing->despawnable = ( rec.flags & NET_MOB_FLAG_DESPAWNABLE ) ? 1 : 0;
            existing->natural_id = net_is_natural_mob_id( rec.id ) ? rec.id : 0;
            existing->touched = ( rec.flags & NET_MOB_FLAG_TOUCHED ) != 0;
            existing->sheared = ( rec.flags & NET_MOB_FLAG_SHEARED ) ? 1 : 0;
            if ( rec.flags & NET_MOB_FLAG_BABY ) {
                existing->age_ticks = std::min<int32_t>( rec.age_ticks, -1 );
            } else {
                existing->breed_cooldown = std::max<int32_t>( rec.age_ticks, 0 );
                existing->age_ticks = 0;
            }
        } else {
            this->spawn_record( rec, !owned );
            Mob *sp = this->find_by_net_id( rec.id );
            if ( sp != nullptr ) {
                sp->net.chunk = chunk;
            }
        }
    }

    // Freshly owned and the server hasn't seen a roll this coverage
    // (populated==0): re-run the deterministic roll on the live chunk.
    // Pristine naturals are dropped on uncover, so a re-cover must re-roll;
    // tombstoned indices (removed_natural) and ids already live locally
    // are skipped. CHUNK_POPULATED reports the roll either way.
    if ( owned && !populated && !this->test_mode && this->net != nullptr ) {
        Chunk *live = state.world.get_loaded_chunk( chunk * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 );
        if ( live != nullptr && live->load_state == CHUNK_LIVE ) {
            live->pending_mobs.clear( );
            const std::set<uint8_t> skip( removed_natural.begin( ), removed_natural.end( ) );
            live->spawn_mobs( &skip );
            for ( const MobFileRecord &record : live->pending_mobs ) {
                if ( record.type >= MOB_TYPE_COUNT || !net_is_natural_mob_id( record.net_id ) ||
                     this->find_by_net_id( record.net_id ) != nullptr || !this->under_mob_cap( ) ) {
                    continue;
                }
                if ( this->spawn_any( static_cast<MobType>( record.type ), glm::dvec3( record.x, record.y, record.z ), record.yaw, 1 ) ) {
                    Mob &mob = this->mobs.back( );
                    mob.variant = record.variant;
                    mob.health = record.health;
                    mob.net.id = record.net_id;
                    mob.natural_id = record.net_id;
                    mob.net_natural = true;
                    mob.net_flags |= NET_MOB_FLAG_NATURAL;
                    mob.touched = ( record.flags & NET_MOB_FLAG_TOUCHED ) != 0;
                    NetEntityRecord rec;
                    rec.id = mob.net.id;
                    rec.kind = NetEntityKind::MOB;
                    rec.x = mob.pos.x;
                    rec.y = mob.pos.y;
                    rec.z = mob.pos.z;
                    rec.yaw = mob.yaw;
                    rec.type = mob.type;
                    rec.count = mob.variant;
                    rec.health = mob.health;
                    rec.flags = mob_net_flags( mob );
                    this->net->send_spawn( rec );
                }
            }
            live->pending_mobs.clear( );
            live->mobs_spawned = 1;
        }
        this->net->send_chunk_populated( chunk );
    }
}

void MobManager::collect_net_state( NetEntities &net, std::vector<NetEntityStateEntry> &out, const long tick_number ) {
    for ( Mob &mob : this->mobs ) {
        if ( mob.net.id == 0 || mob.net.mirror || mob.uid != 0 ) {
            continue;
        }
        if ( !net.owns_chunk( mob.net.chunk ) ) {
            continue;
        }
        const uint32_t flags = mob_net_flags( mob );
        const bool keyframe = netsync_keyframe_due( mob.net, tick_number );
        const bool changed = !mob.net.sent ||                                 //
                             netsync_moved( mob.net, mob.pos, mob.vel ) ||    //
                             fabsf( mob.yaw - mob.net_sent_yaw ) > 1e-4f ||   //
                             mob.health != mob.net_sent_health ||             //
                             flags != mob.net_sent_flags;
        if ( !changed && !keyframe ) {
            continue;
        }
        NetEntityStateEntry e;
        e.id = mob.net.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::MOB );
        e.x = mob.pos.x;
        e.y = mob.pos.y;
        e.z = mob.pos.z;
        e.vx = static_cast<float>( mob.vel.x );
        e.vy = static_cast<float>( mob.vel.y );
        e.vz = static_cast<float>( mob.vel.z );
        e.yaw = mob.yaw;
        e.count = mob.variant;
        e.health = mob.health;
        e.flags = flags;
        out.push_back( e );
        mob.net_sent_yaw = mob.yaw;
        mob.net_sent_health = mob.health;
        mob.net_sent_flags = flags;
        // The server now applies its boundary hysteresis to this pos.
        netsync_mark_sent( mob.net, mob.pos, mob.vel, tick_number, keyframe );
    }
}
