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

MobManager::MobManager( ) {
    this->enabled = env_bool( "REPGAME_MOBS", true );
    this->spawner_enabled = env_bool( "REPGAME_MOB_SPAWNER", true ) && !repgame_block_world( );
    this->test_mode = env_is_set( "REPGAME_MOB_TEST" );
    this->render_inited = false;
    this->storage = nullptr;
    this->poll_count = 0;
    this->spawn_ticks = 0;
    this->next_uid = 1;
    this->rng.seed( 0x9e3779b9u );
}

void MobManager::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance,
                       const VertexBufferLayout &vbl_block_object_instance, const VertexBufferLayout &vbl_item_instance ) {
    // Rendering is always available (remote players draw even under
    // REPGAME_MOBS=0); `enabled` only gates spawning, ticking and persistence.
    this->render_chain.init( vbl_object_vertex, vbl_mob_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    // Held items share the dropped-item meshes (item_entities.cpp).
    this->render_held_cube.init( vbl_object_vertex, vbl_block_object_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    int sv = 0, si = 0;
    const ParticleVertex *sprite_vb = item_sprite_mesh_vertices( &sv );
    const unsigned int *sprite_ib = item_sprite_mesh_indices( &si );
    this->render_held_sprite.init( vbl_object_vertex, vbl_item_instance, sprite_vb, sv, sprite_ib, si );
    this->render_inited = true;

    // REPGAME_MOB_TEST="pig,x,y,z,yaw;..." — exact spawns for deterministic
    // captures and physics tests. Remote types may take a trailing held-item
    // name: "player,x,y,z,yaw,pitch,sneak,torch".
    const char *spec = env_get( "REPGAME_MOB_TEST" );
    if ( spec != NULL && this->enabled ) {
        char buf[ 1024 ];
        strncpy( buf, spec, sizeof( buf ) - 1 );
        buf[ sizeof( buf ) - 1 ] = 0;
        for ( char *entry = strtok( buf, ";" ); entry != NULL; entry = strtok( NULL, ";" ) ) {
            char type_name[ 32 ];
            char held_name[ 32 ] = "";
            float x, y, z, yaw = 0.0f, pitch = 0.0f, sneak = 0.0f;
            if ( sscanf( entry, "%31[^,],%f,%f,%f,%f,%f,%f,%31[^,]", type_name, &x, &y, &z, &yaw, &pitch, &sneak, held_name ) < 4 ) {
                pr_debug( "REPGAME_MOB_TEST: bad entry '%s' (want type,x,y,z,yaw[,pitch[,sneak[,item]]])", entry );
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
                if ( held_name[ 0 ] != 0 ) {
                    const ItemID held = item_id_from_name( held_name );
                    if ( held == LAST_BLOCK_ID ) {
                        pr_debug( "REPGAME_MOB_TEST: unknown held item '%s'", held_name );
                    } else {
                        this->set_remote_held_item( uid, held );
                    }
                }
                continue;
            }
            // MOB_TEST spawns are non-persistent: they never reach a mobs file
            // and natural spawn/load is disabled for the whole run.
            this->spawn( type, glm::dvec3( x, y, z ), yaw, 0, false );
        }
    }
    // Block world: a static mob line-up next to the block grid (MOB_TEST
    // wins when both are set — its fixtures are the authored showcase).
    // The mob world fills its arena instead.
    if ( repgame_block_world( ) && this->enabled && spec == NULL ) {
        if ( repgame_mob_world( ) ) {
            this->init_stress( mob_stress_spec( ) );
        } else {
            this->spawn_block_world_showcase( );
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
        // Seed-derived naturals don't count: the deterministic roll bounds
        // the farm-animal population, not MAX_MOBS (a pen's touched naturals
        // keep their natural_id, so livestock is exempt too).
        total += mob.uid == 0 && !mob.net.mirror && mob.natural_id == 0;
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
    mob.leg_locked = 0;
    mob.blocked_ticks = 0;
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
    mob.no_ai = 0;
    mob.crowd_collides = 1;
    std::uniform_int_distribution<int> say( 10 * UPS_RATE, 30 * UPS_RATE );
    mob.say_ticks = say( this->rng );
    mob.step_acc = 0.0f;
    mob.kb = glm::dvec3( 0.0 );
    mob.despawnable = 0;
    mob.targeting = 0;
    mob.attack_ticks = 0;
    mob.strafe_ticks = 0;
    mob.strafe_dir = 1;
    mob.fuse_ticks = 0;
    mob.burn_acc = 0;
    std::uniform_real_distribution<float> phase_pick( 0.0f, 6.2832f );
    mob.wing_phase = phase_pick( this->rng );
    mob.anim_phase = phase_pick( this->rng );
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
    mob.seated = 0;
    mob.seat_yaw = 0.0f;
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
    // Type-fixed held item (the skeleton's bow); remote players get theirs
    // per-avatar from NetPlayerPayload instead.
    mob.held_item = def.held_item;
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

bool MobManager::spawn_in_front( MobType type, const glm::dvec3 &camera_pos, const glm::dvec3 &look, World &world ) {
    const glm::dvec3 at = camera_pos + look * 3.0;
    const int bx = static_cast<int>( floor( at.x ) );
    const int bz = static_cast<int>( floor( at.z ) );
    bool found = false;
    const double ground = mob_terrain::ground_top_at( world, bx, bz, static_cast<int>( floor( at.y ) ), 30, &found );
    // Explicit player spawn: uncapped like an egg.
    return this->spawn( type, glm::dvec3( at.x, found ? ground : at.y, at.z ), 0.0f, 1, false );
}

// Block world: one of each mob type in a column at x=-3, spaced 3 apart in
// z on the empty y=1 plane west of the block grid, all facing +x toward it.
// Local types get `no_ai` so they just stand there (still hittable and
// killable); the MOB_AI_REMOTE player avatar is spawned and posed like a
// REPGAME_MOB_TEST remote entry.
void MobManager::spawn_block_world_showcase( ) {
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        const MobType type = static_cast<MobType>( t );
        const glm::dvec3 pos( -2.5, 1.0, 2.5 + 3.0 * t );
        if ( mob_types[ t ].ai == MOB_AI_REMOTE ) {
            const uint32_t uid = this->spawn_remote( type );
            this->set_remote_pose( uid, pos + glm::dvec3( 0.0, PLAYER_EYE_HEIGHT, 0.0 ), 90.0f, 0.0f );
            continue;
        }
        if ( this->spawn( type, pos, 90.0f, 0, false ) ) {
            this->mobs.back( ).no_ai = 1;
        }
    }
}

void MobManager::init_stress( const MobStressSpec &spec ) {
    this->stress = spec;
    this->stress_target = spec.count;
    this->stress_ai = spec.ai;
    this->stress_next = 0;
    this->stress_ticks = 0;
    if ( !spec.enabled( ) ) {
        return;
    }
    this->stress_rebalance( );
    pr_debug( "mob world: %zu mobs (%zu types, capacity %d, arena %dx%d, ai %s)", this->mobs.size( ), spec.types.size( ),
              spec.capacity, spec.extent( ), spec.extent( ), spec.ai ? "on" : "off" );
}

// One stress mob on the next grid cell, cycling the spec's types. Remote
// avatars (MOB_AI_REMOTE) are posed like REPGAME_MOB_TEST entries and hold
// a torch so the held-item chains are exercised too.
void MobManager::stress_spawn_one( ) {
    const long n = this->stress_next++;
    const MobType type = this->stress.types[ static_cast<size_t>( n ) % this->stress.types.size( ) ];
    const glm::dvec3 pos = mob_stress_cell_pos( this->stress, static_cast<int>( n % this->stress.capacity ) );
    std::uniform_real_distribution<float> pick_yaw( -180.0f, 180.0f );
    const float yaw = pick_yaw( this->rng );
    if ( mob_types[ type ].ai == MOB_AI_REMOTE ) {
        const uint32_t uid = this->spawn_remote( type );
        if ( uid != 0 ) {
            this->set_remote_pose( uid, pos + glm::dvec3( 0.0, PLAYER_EYE_HEIGHT, 0.0 ), yaw, 0.0f );
            static const ItemID torch = item_id_from_name( "torch" );
            if ( torch != LAST_BLOCK_ID ) {
                this->set_remote_held_item( uid, torch );
            }
        }
        return;
    }
    if ( this->spawn( type, pos, yaw, 0, false ) ) {
        this->mobs.back( ).no_ai = this->stress_ai ? 0 : 1;
    }
}

// Trim the newest mobs past the target, then refill up to it.
void MobManager::stress_rebalance( ) {
    const size_t target = static_cast<size_t>( this->stress_target );
    while ( this->mobs.size( ) > target ) {
        this->remove_mob( this->mobs.end( ) - 1 );
    }
    while ( this->mobs.size( ) < target ) {
        const size_t before = this->mobs.size( );
        this->stress_spawn_one( );
        if ( this->mobs.size( ) == before ) {
            break; // spawning refused (mobs disabled) — don't spin
        }
    }
}

void MobManager::set_stress_target( const int target ) {
    if ( !this->stress.enabled( ) ) {
        return;
    }
    this->stress_target = target < 0 ? 0 : ( target > this->stress.capacity ? this->stress.capacity : target );
    this->stress_rebalance( );
}

void MobManager::set_stress_ai( const bool ai ) {
    this->stress_ai = ai;
    for ( Mob &mob : this->mobs ) {
        if ( mob.uid == 0 ) {
            mob.no_ai = ai ? 0 : 1;
        }
    }
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
            // A seated avatar's position comes from its cart
            // (set_remote_seated); a streamed pose only steers the head.
            if ( !mob.seated ) {
                mob.target = eye_pos - glm::dvec3( 0.0, static_cast<double>( eye_height ), 0.0 );
            }
            mob.look_yaw = look_yaw_deg;
            mob.look_pitch = look_pitch_deg;
            mob.sneak_target = glm::clamp( ( PLAYER_EYE_HEIGHT - eye_height ) / PLAYER_SNEAK_EYE_DROP, 0.0f, 1.0f );
            return;
        }
    }
}

void MobManager::set_remote_seated( const uint32_t uid, const glm::dvec3 &seat_pos, const float cart_yaw, const bool seated ) {
    for ( Mob &mob : this->mobs ) {
        if ( mob.uid == uid ) {
            mob.seated = seated;
            if ( seated ) {
                mob.target = seat_pos;
                mob.seat_yaw = cart_yaw;
            }
            return;
        }
    }
}

void MobManager::set_remote_crowd_collides( const uint32_t uid, const bool collides ) {
    for ( Mob &mob : this->mobs ) {
        if ( mob.uid == uid ) {
            mob.crowd_collides = collides ? 1 : 0;
            return;
        }
    }
}

void MobManager::set_remote_held_item( const uint32_t uid, const ItemID item ) {
    for ( Mob &mob : this->mobs ) {
        if ( mob.uid == uid ) {
            mob.held_item = item;
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

bool MobManager::remote_head_pos( const uint32_t uid, const float alpha, glm::dvec3 &out, float &sneak ) const {
    for ( const Mob &mob : this->mobs ) {
        if ( mob.uid == uid ) {
            const glm::dvec3 feet = glm::mix( mob.prev_pos, mob.pos, static_cast<double>( alpha ) );
            out = feet + glm::dvec3( 0.0, mob_types[ mob.type ].hitbox_h * scale_of( mob ), 0.0 );
            sneak = mob.prev_sneak + ( mob.sneak - mob.prev_sneak ) * alpha;
            return true;
        }
    }
    return false;
}

void MobManager::flash_remote_hurt( const uint32_t uid, const glm::dvec3 &listener_pos ) {
    for ( Mob &mob : this->mobs ) {
        if ( mob.uid == uid ) {
            mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
            audio_play_sfx_gain( "player_hurt", audio_distance_gain( glm::distance( mob.pos, listener_pos ) ) );
            return;
        }
    }
}
