#include "common/mobs.hpp"

#include <cmath>
#include <cstring>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/chunk_loader.hpp"
#include "common/utils/collision.hpp"
#include "common/world.hpp"

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
    this->test_mode = getenv( "REPGAME_MOB_TEST" ) != nullptr;
    this->render_inited = false;
    this->storage = nullptr;
    this->poll_count = 0;
    this->rng.seed( 0x9e3779b9u );
}

void MobManager::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance ) {
    if ( !this->enabled ) {
        return;
    }
    this->render_chain.init( vbl_object_vertex, vbl_mob_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_inited = true;

    // REPGAME_MOB_TEST="pig,x,y,z,yaw;..." — exact spawns for deterministic
    // captures and physics tests.
    const char *spec = getenv( "REPGAME_MOB_TEST" );
    if ( spec != NULL ) {
        char buf[ 1024 ];
        strncpy( buf, spec, sizeof( buf ) - 1 );
        buf[ sizeof( buf ) - 1 ] = 0;
        for ( char *entry = strtok( buf, ";" ); entry != NULL; entry = strtok( NULL, ";" ) ) {
            char type_name[ 32 ];
            float x, y, z, yaw = 0.0f;
            if ( sscanf( entry, "%31[^,],%f,%f,%f,%f", type_name, &x, &y, &z, &yaw ) < 4 ) {
                pr_debug( "REPGAME_MOB_TEST: bad entry '%s' (want type,x,y,z,yaw)", entry );
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
            // MOB_TEST spawns are non-persistent: they never reach a mobs file
            // and natural spawn/load is disabled for the whole run.
            this->spawn( type, glm::dvec3( x, y, z ), yaw, 0 );
        }
    }
}

bool MobManager::spawn( MobType type, const glm::dvec3 &pos, float yaw, int persistent ) {
    if ( !this->enabled || this->mobs.size( ) >= MAX_MOBS ) {
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
    mob.persistent = persistent;
    std::uniform_int_distribution<int> say( 10 * UPS_RATE, 30 * UPS_RATE );
    mob.say_ticks = say( this->rng );
    mob.step_acc = 0.0f;
    mob.kb = glm::dvec3( 0.0 );
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

void MobManager::tick_mob( World &world, Mob &mob, const MobTypeDef &def, const glm::dvec3 &player_pos ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    if ( mob.hurt_ticks > 0 ) {
        mob.hurt_ticks--;
    }
    if ( mob.death_ticks > 0 ) {
        mob.death_ticks++;
        return;
    }

    const glm::ivec3 feet = glm::ivec3( glm::floor( mob.pos ) );
    const bool in_water = world.get_loaded_block( feet ).id == WATER || world.get_loaded_block( feet + glm::ivec3( 0, 1, 0 ) ).id == WATER;

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
    if ( def.ai == MOB_AI_PASSIVE_WANDER ) {
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
            audio_play_sfx_gain( def.sound_step, 1.0f - static_cast<float>( dist ) / 16.0f );
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
        if ( mob.death_ticks > 0 ) {
            continue;
        }
        const double hw = mob_types[ mob.type ].hitbox_w / 2.0;
        const glm::dvec3 bmin( mob.pos.x - hw, mob.pos.y, mob.pos.z - hw );
        const glm::dvec3 bmax( mob.pos.x + hw, mob.pos.y + mob_types[ mob.type ].hitbox_h, mob.pos.z + hw );
        double t0 = 0.0, t1 = best_t;
        bool hit = true;
        for ( int a = 0; a < 3 && hit; a++ ) {
            const double o = origin[ a ], d = dir[ a ];
            if ( fabs( d ) < 1e-9 ) {
                hit = o >= bmin[ a ] && o <= bmax[ a ];
            } else {
                double ta = ( bmin[ a ] - o ) / d, tb = ( bmax[ a ] - o ) / d;
                if ( ta > tb ) {
                    const double tmp = ta;
                    ta = tb;
                    tb = tmp;
                }
                t0 = ta > t0 ? ta : t0;
                t1 = tb < t1 ? tb : t1;
                hit = t0 <= t1;
            }
        }
        if ( hit ) {
            best_t = t0;
            best = static_cast<int>( i );
        }
    }
    if ( best >= 0 && out_dist != nullptr ) {
        *out_dist = best_t;
    }
    return best;
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
    mob.health -= damage;
    mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
    mob.panic_ticks = 5 * UPS_RATE;
    mob.ai_state = 0;
    const MobTypeDef &def = mob_types[ mob.type ];
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
            if ( it->persistent && feet_chunk == owned.pos ) {
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
            if ( mob.persistent && glm::ivec3( glm::floor( mob.pos / csize ) ) == owned.pos ) {
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

void MobManager::tick( World &world, const glm::dvec3 &player_pos ) {
    if ( !this->enabled || RepGame::capture_mode( ) ) {
        return;
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
        // A mob whose feet chunk isn't live is frozen: parked/released slots
        // report LAST_BLOCK_ID from get_loaded_block and a LOADING slot at the
        // same position hasn't finished adopting back yet.
        const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( it->pos / csize ) );
        const Chunk *chunk = world.chunkLoader.get_chunk( feet_chunk );
        if ( chunk == nullptr || chunk->load_state != CHUNK_LIVE ) {
            it->prev_pos = it->pos;
            it->prev_yaw = it->yaw;
            ++it;
            continue;
        }
        if ( it->death_ticks > 0 && it->death_ticks > static_cast<int>( 0.5f * UPS_RATE ) ) {
            it = this->remove_mob( it );
            continue;
        }
        this->tick_mob( world, *it, mob_types[ it->type ], player_pos );
        ++it;
    }
    // Owned-chunk leave-poll (~every 16 ticks).
    if ( ++this->poll_count >= 16 ) {
        this->poll_count = 0;
        this->poll_owned( world );
    }
}

// Bone rest + animator rotation, in degrees. bone_index lets the animators
// bind to the type's named bones (resolved once per mob by update_render).
static glm::mat4 bone_local_matrix( const MobBoneDef &bone, const glm::vec3 &anim_rot ) {
    const glm::vec3 pivot( bone.pivot[ 0 ], bone.pivot[ 1 ], bone.pivot[ 2 ] );
    const glm::vec3 rot( bone.rest_rot[ 0 ] + anim_rot.x, bone.rest_rot[ 1 ] + anim_rot.y, bone.rest_rot[ 2 ] + anim_rot.z );
    glm::mat4 m = glm::translate( glm::mat4( 1.0f ), pivot );
    m = glm::rotate( m, glm::radians( rot.x ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    m = glm::rotate( m, glm::radians( rot.y ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    m = glm::rotate( m, glm::radians( rot.z ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
    return glm::translate( m, -pivot );
}

void MobManager::update_render( float alpha, const ChunkLoader &chunk_loader ) {
    if ( !this->enabled || !this->render_inited ) {
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
        base = glm::scale( base, glm::vec3( 1.0f / 16.0f ) );

        // Per-bone animator rotations, resolved by name from the type table.
        glm::vec3 bone_anim[ 32 ];
        const int bone_count = model.bone_count < 32 ? model.bone_count : 32;
        for ( int i = 0; i < bone_count; i++ ) {
            bone_anim[ i ] = glm::vec3( 0.0f );
        }
        const float phase = glm::radians( mob.walk_dist * 38.17f );
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            if ( ( def.anims & MOB_ANIM_HEAD_LOOK ) && def.bone_head != NULL && strcmp( bone.name, def.bone_head ) == 0 ) {
                bone_anim[ i ].y = -mob.head_yaw;
                bone_anim[ i ].x = -mob.head_pitch;
            }
            if ( def.anims & MOB_ANIM_QUADRUPED_WALK ) {
                for ( int leg = 0; leg < 4; leg++ ) {
                    if ( def.bone_legs[ leg ] != NULL && strcmp( bone.name, def.bone_legs[ leg ] ) == 0 ) {
                        const float sign = ( leg == 0 || leg == 3 ) ? 1.0f : -1.0f;
                        bone_anim[ i ].x += sign * 80.0f * cosf( phase ) * mob.leg_amp;
                    }
                }
            }
        }

        // One light sample per mob covers all of its parts.
        const glm::vec2 light = chunk_loader.light_sample( glm::vec3( rpos ) + glm::vec3( 0.0f, def.eye_h, 0.0f ) );

        glm::mat4 bone_world[ 32 ];
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            const glm::mat4 local = bone_local_matrix( bone, bone_anim[ i ] );
            bone_world[ i ] = bone.parent >= 0 ? bone_world[ bone.parent ] * local : local;
        }

        int part = 0;
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
                inst.uv_b = glm::vec4( cube.size[ 2 ], static_cast<float>( def.skins[ mob.variant ] ),
                                       ( bone.mirror || cube.mirror ) ? 1.0f : 0.0f, mob.hurt_ticks > 0 ? -0.7f : 0.0f );
            }
        }
    }
    this->render_chain.invalidate_all( );
}

void MobManager::draw( const Renderer &renderer, const Shader &shader ) {
    if ( !this->enabled || !this->render_inited ) {
        return;
    }
    this->render_chain.draw( renderer, shader );
}

void MobManager::cleanup( ) {
    this->render_chain.clear( );
    this->mobs.clear( );
}
