#include "common/mobs.hpp"

#include <cmath>
#include <cstring>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include "common/light.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/world.hpp"

// Bedrock modified_distance_moved / Java limbSwing advance about 4 units per
// block; the quadruped walk anim's 38.17°/unit is calibrated against that.
#define MOB_GAIT_UNITS_PER_BLOCK 4.0f

static float wrap_degrees( float deg ) {
    while ( deg > 180.0f ) {
        deg -= 360.0f;
    }
    while ( deg < -180.0f ) {
        deg += 360.0f;
    }
    return deg;
}

MobManager::MobManager( ) {
    this->enabled = !( getenv( "REPGAME_MOBS" ) && atoi( getenv( "REPGAME_MOBS" ) ) == 0 );
    this->spawner_enabled = !( getenv( "REPGAME_MOB_SPAWNER" ) && atoi( getenv( "REPGAME_MOB_SPAWNER" ) ) == 0 );
    this->test_mode = getenv( "REPGAME_MOB_TEST" ) != nullptr;
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
    const char *spec = getenv( "REPGAME_MOB_TEST" );
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
            this->spawn( type, glm::dvec3( x, y, z ), yaw, 0 );
        }
    }
}

bool MobManager::spawn( MobType type, const glm::dvec3 &pos, float yaw, int persistent ) {
    if ( !this->enabled ) {
        return false;
    }
    return this->spawn_any( type, pos, yaw, persistent );
}

bool MobManager::spawn_any( MobType type, const glm::dvec3 &pos, float yaw, int persistent ) {
    if ( this->mobs.size( ) >= MAX_MOBS ) {
        return false;
    }
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
    mob.uid = 0;
    mob.look_yaw = 0.0f;
    mob.look_pitch = 0.0f;
    if ( this->render_inited ) {
        const MobModelDef &model = mob_models[ def.model ];
        for ( int i = 0; i < model.cube_count; i++ ) {
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

bool MobManager::spawn_in_front( MobType type, const glm::dvec3 &camera_pos, const glm::dvec3 &look, World &world ) {
    const glm::dvec3 at = camera_pos + look * 3.0;
    const int bx = static_cast<int>( floor( at.x ) );
    const int bz = static_cast<int>( floor( at.z ) );
    bool found = false;
    const double ground = ground_top_at( world, bx, bz, static_cast<int>( floor( at.y ) ), 30, &found );
    return this->spawn( type, glm::dvec3( at.x, found ? ground : at.y, at.z ), 0.0f, 1 );
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
        mob.yaw += wrap_degrees( want_yaw - mob.yaw ) * 0.2f;
    }
    // The head can't lead the body by more than 50°: drag the body along.
    const float lead = wrap_degrees( mob.look_yaw - mob.yaw );
    if ( lead > 50.0f ) {
        mob.yaw = wrap_degrees( mob.look_yaw - 50.0f );
    } else if ( lead < -50.0f ) {
        mob.yaw = wrap_degrees( mob.look_yaw + 50.0f );
    }
    mob.head_yaw = wrap_degrees( mob.look_yaw - mob.yaw );
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
        return;
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

    // Hostile targeting: acquire inside 16 blocks of the player's feet, drop
    // past 24. A non-targetable player means passive wandering.
    if ( hostile ) {
        if ( !in->player_targetable ) {
            mob.targeting = 0;
        } else if ( !mob.targeting && player_dist_xz < 16.0 ) {
            mob.targeting = 1;
        } else if ( mob.targeting && player_dist_xz > 24.0 ) {
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

    // Nearest player look-at (single-player: the one camera position).
    const glm::dvec3 mob_eye = mob.pos + glm::dvec3( 0.0, def.eye_h, 0.0 );
    {
        const glm::dvec3 to_player = player_pos - mob_eye;
        const double dist_xz = std::hypot( to_player.x, to_player.z );
        float want_yaw = 0.0f, want_pitch = 0.0f;
        if ( glm::length( to_player ) < 8.0 && dist_xz > 1e-3 ) {
            const float pyaw = static_cast<float>( atan2( to_player.x, -to_player.z ) * ( 180.0 / M_PI ) );
            want_yaw = glm::clamp( wrap_degrees( pyaw - mob.yaw ), -45.0f, 45.0f );
            want_pitch = glm::clamp( static_cast<float>( atan2( to_player.y, dist_xz ) * ( 180.0 / M_PI ) ), -45.0f, 45.0f );
        }
        mob.head_yaw += ( want_yaw - mob.head_yaw ) * 0.2f;
        mob.head_pitch += ( want_pitch - mob.head_pitch ) * 0.2f;
    }

    float speed = 0.0f;
    if ( hostile && mob.targeting ) {
        // Chase: face the player's feet, turning at most 10°/tick.
        const glm::dvec2 to_player( player_feet.x - mob.pos.x, player_feet.z - mob.pos.z );
        const float want_yaw = static_cast<float>( atan2( to_player.x, -to_player.y ) * ( 180.0 / M_PI ) );
        mob.yaw += glm::clamp( wrap_degrees( want_yaw - mob.yaw ), -10.0f, 10.0f );
        speed = def.chase_speed;
        if ( def.ai == MOB_AI_HOSTILE_MELEE ) {
            // Melee reach: just past the hitbox edge, with a 1 s cooldown.
            if ( mob.attack_ticks > 0 ) {
                mob.attack_ticks--;
            }
            if ( mob.attack_ticks <= 0 && player_dist_xz < def.hitbox_w / 2.0 + 0.3 + 0.5 && fabs( player_feet.y - mob.pos.y ) < 1.5 ) {
                mob.attack_ticks = UPS_RATE;
                if ( out != nullptr ) {
                    out->player_hits.push_back( { mob.pos, def.attack_damage } );
                }
            }
        } else {
            // Creeper fuse: lit inside 3 blocks; the mob stands still while
            // fusing and defuses (counts back down) past 7 blocks. The tick
            // loop explodes it at the end of the fuse.
            if ( mob.fuse_ticks > 0 || player_dist_xz < 3.0 ) {
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
        if ( mob.targeting && player_dist_xz <= 7.0 ) {
            mob.fuse_ticks++;
        } else {
            mob.fuse_ticks--;
        }
    }
    if ( def.ai == MOB_AI_PASSIVE_WANDER || ( hostile && !mob.targeting ) ) {
        if ( mob.panic_ticks > 0 ) {
            // Panic: sprint in a fleeing direction, re-picking occasionally.
            mob.panic_ticks--;
            if ( mob.panic_ticks % 15 == 0 ) {
                std::uniform_real_distribution<float> pick_yaw( -180.0f, 180.0f );
                mob.yaw = pick_yaw( this->rng );
            }
            speed = def.panic_speed;
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
            const float dyaw = wrap_degrees( want_yaw - mob.yaw );
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
    glm::dvec3 eye = mob.pos + glm::dvec3( 0.0, def.hitbox_h, 0.0 );
    glm::dvec3 move = mob.vel + mob.kb;
    mob.kb *= 0.85;
    const glm::dvec3 want_move = mob.vel;
    int standing = 0;
    Collision::check_move( world, move, eye, &standing, def.hitbox_h, def.hitbox_h, false, def.hitbox_w / 2.0 );
    mob.pos += move;
    mob.standing = standing;

    if ( standing ) {
        mob.vel.y = 0.0;
    } else {
        mob.vel.y = move.y;
    }

    // Auto-jump: blocked horizontally while standing with a 1-block wall
    // ahead (something step-up can't climb, but a hop can). Must come after
    // the standing reset so the jump velocity survives to next tick.
    const double want_h = std::hypot( want_move.x, want_move.z );
    const double got_h = std::hypot( move.x, move.z );
    if ( standing && want_h > 1e-4 && got_h < want_h * 0.3 ) {
        const glm::dvec3 dir( want_move.x / want_h, 0.0, want_move.z / want_h );
        const glm::dvec3 ahead = mob.pos + dir * ( def.hitbox_w / 2.0 + 0.4 );
        const glm::ivec3 cell( static_cast<int>( floor( ahead.x ) ), feet.y, static_cast<int>( floor( ahead.z ) ) );
        if ( solid_at( world, cell ) && !solid_at( world, cell + glm::ivec3( 0, 1, 0 ) ) && !solid_at( world, cell + glm::ivec3( 0, 2, 0 ) ) ) {
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
        const double hw = mob_types[ mob.type ].hitbox_w / 2.0;
        const glm::dvec3 bmin( mob.pos.x - hw, mob.pos.y, mob.pos.z - hw );
        const glm::dvec3 bmax( mob.pos.x + hw, mob.pos.y + mob_types[ mob.type ].hitbox_h, mob.pos.z + hw );
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

// Called from World::render for every chunk that finished load+finalize this
// frame. Claims ownership (so the leave path knows whether to persist) and
// spawns the worker-side pending_mobs as live mobs.
void MobManager::adopt_chunk( Chunk &chunk ) {
    if ( !this->enabled || this->test_mode ) {
        chunk.pending_mobs.clear( );
        return;
    }
    for ( const OwnedChunk &owned : this->owned_chunks ) {
        if ( owned.pos == chunk.chunk_pos ) {
            chunk.pending_mobs.clear( );
            return;
        }
    }
    for ( const MobFileRecord &record : chunk.pending_mobs ) {
        if ( this->spawn( static_cast<MobType>( record.type ), glm::dvec3( record.x, record.y, record.z ), record.yaw, 1 ) ) {
            Mob &mob = this->mobs.back( );
            mob.variant = record.variant;
            mob.health = record.health;
        }
    }
    this->owned_chunks.push_back( { chunk.chunk_pos, chunk.mobs_file_present, chunk.mobs_spawned } );
    chunk.pending_mobs.clear( );
}

// Roughly every 16 ticks: a chunk has left when its slot is empty, carries a
// different position, or is PARKED (deferred release already claimed it).
// CHUNK_LOADING at the same pos has not left — the slot is being reused for
// the same position.
void MobManager::poll_owned( World &world ) {
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
                MobFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u };
                record.type = static_cast<uint16_t>( it->type );
                record.variant = static_cast<uint16_t>( it->variant < 0 ? 0 : it->variant );
                record.x = it->pos.x;
                record.y = it->pos.y;
                record.z = it->pos.z;
                record.yaw = it->yaw;
                record.health = it->health;
                records.push_back( record );
                it = this->remove_mob( it );
            } else {
                ++it;
            }
        }
        // Write only if there is something worth recording: live mobs, a
        // file we loaded, or a spawn roll that produced mobs. An owned chunk
        // that never had either leaves no file behind.
        // No writes in capture or fly mode — capture exits without player.dat
        // and fly is a throwaway scripted run; both must leave no mob files.
        if ( this->storage != nullptr && !RepGame::capture_mode( ) && !RepGame::fly_mode( ) &&
             ( !records.empty( ) || owned.file_present || owned.spawned ) ) {
            this->storage->persist_mobs( owned.pos, records );
        }
        this->owned_chunks.erase( this->owned_chunks.begin( ) + static_cast<long>( i ) );
    }
}

void MobManager::persist_all( ) {
    if ( this->storage == nullptr || RepGame::capture_mode( ) || RepGame::fly_mode( ) ) {
        return;
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( const OwnedChunk &owned : this->owned_chunks ) {
        std::vector<MobFileRecord> records;
        for ( const Mob &mob : this->mobs ) {
            if ( mob.persistent && !mob.despawnable && glm::ivec3( glm::floor( mob.pos / csize ) ) == owned.pos ) {
                MobFileRecord record = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u };
                record.type = static_cast<uint16_t>( mob.type );
                record.variant = static_cast<uint16_t>( mob.variant < 0 ? 0 : mob.variant );
                record.x = mob.pos.x;
                record.y = mob.pos.y;
                record.z = mob.pos.z;
                record.yaw = mob.yaw;
                record.health = mob.health;
                records.push_back( record );
            }
        }
        if ( !records.empty( ) || owned.file_present || owned.spawned ) {
            this->storage->persist_mobs( owned.pos, records );
        }
    }
}

void MobManager::tick( World &world, const glm::dvec3 &player_pos, const MobTickInput *in, MobTickOutput *out ) {
    // Remote mobs tick in every mode (they're driven by the network, not the
    // world); everything else freezes under REPGAME_MOBS=0 and capture mode.
    const bool freeze = !this->enabled || RepGame::capture_mode( );
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    const float player_eye = in != nullptr ? in->player_eye_height : PLAYER_EYE_HEIGHT;
    const glm::dvec3 player_feet = player_pos - glm::dvec3( 0.0, player_eye, 0.0 );
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
        if ( it->uid != 0 ) {
            this->tick_remote( *it );
            ++it;
            continue;
        }
        // Night-spawner mobs vanish instead of freezing or lingering: past 64
        // blocks outright, a slow random roll past 40, or when their feet
        // chunk leaves the live window.
        if ( it->despawnable ) {
            const double dist = glm::distance( it->pos, player_feet );
            std::uniform_real_distribution<double> roll( 0.0, 1.0 );
            if ( dist > 64.0 || ( dist > 40.0 && roll( this->rng ) < 1.0 / ( 8.0 * UPS_RATE ) ) ) {
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
                it = this->remove_mob( it );
                continue;
            }
            it->prev_pos = it->pos;
            it->prev_yaw = it->yaw;
            ++it;
            continue;
        }
        if ( it->death_ticks > 0 && it->death_ticks > static_cast<int>( 0.5f * UPS_RATE ) ) {
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
            it = this->remove_mob( it );
            continue;
        }
        ++it;
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
    int live = 0;
    for ( const Mob &mob : this->mobs ) {
        live += mob.despawnable;
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
        const glm::vec2 light = world.chunkLoader.light_sample( glm::vec3( feet_pos ) + glm::vec3( 0.0f, 0.5f, 0.0f ) );
        if ( !hostile_spawn_light_ok( light.x, light.y, daylight ) ) {
            continue;
        }
        const MobType type = type_roll( this->rng ) < 0.6 ? MOB_ZOMBIE : MOB_CREEPER;
        if ( this->spawn_any( type, feet_pos, yaw_pick( this->rng ), 0 ) ) {
            this->mobs.back( ).despawnable = 1;
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
    const MobModelDef &model = mob_models[ def.model ];
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

void MobManager::update_render( float alpha, const ChunkLoader &chunk_loader ) {
    if ( !this->render_inited ) {
        return;
    }
    for ( Mob &mob : this->mobs ) {
        const MobTypeDef &def = mob_types[ mob.type ];
        const MobModelDef &model = mob_models[ def.model ];

        const glm::dvec3 rpos = glm::mix( mob.prev_pos, mob.pos, static_cast<double>( alpha ) );
        const float ryaw = mob.prev_yaw + wrap_degrees( mob.yaw - mob.prev_yaw ) * alpha;

        // entity_T * yaw_R * scale(1/16): the model's -Z front lands on the
        // mob's forward direction (sin(yaw), 0, -cos(yaw)) — the same
        // convention as the player's angle_H. The unit cube's z=0 verts
        // (FACE_BACK tag) are the model's front; mob_face_rect maps that.
        glm::mat4 base = glm::translate( glm::mat4( 1.0f ), glm::vec3( rpos ) );
        base = glm::rotate( base, glm::radians( -ryaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
        if ( mob.death_ticks > 0 ) {
            // Tip-over: roll 90° onto the side over 0.5 s, pivoting at the
            // hitbox's mid height.
            const float roll = fminf( mob.death_ticks / ( 0.5f * UPS_RATE ), 1.0f ) * 90.0f;
            base = glm::translate( base, glm::vec3( 0.0f, def.hitbox_h * 0.4f, 0.0f ) );
            base = glm::rotate( base, glm::radians( roll ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
            base = glm::translate( base, glm::vec3( 0.0f, -def.hitbox_h * 0.4f, 0.0f ) );
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
        base = glm::scale( base, glm::vec3( 1.0f / 16.0f ) );

        // One light sample per mob covers all of its parts.
        const glm::vec2 light = chunk_loader.light_sample( glm::vec3( rpos ) + glm::vec3( 0.0f, def.eye_h, 0.0f ) );

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
    }
    this->render_chain.invalidate_all( );
}

void MobManager::draw( const Renderer &renderer, const Shader &shader ) {
    if ( !this->render_inited ) {
        return;
    }
    this->render_chain.draw( renderer, shader );
}

void MobManager::cleanup( ) {
    this->render_chain.clear( );
    this->mobs.clear( );
}
