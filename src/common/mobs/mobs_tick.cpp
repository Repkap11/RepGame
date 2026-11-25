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

void MobManager::tick( World &world, const glm::dvec3 &player_pos, const MobTickInput *in, MobTickOutput *out ) {
    PROFILE_SCOPE( "mobs_tick" );
    // Remote mobs tick in every mode (they're driven by the network, not the
    // world); everything else freezes under REPGAME_MOBS=0 and capture mode.
    const bool freeze = !this->enabled || RepGame::capture_mode( );
    // Mob world: hold the population at the target (deaths refill once a
    // second, in the same grid/type rotation).
    if ( this->stress.enabled( ) && !freeze && ++this->stress_ticks % UPS_RATE == 0 ) {
        this->stress_rebalance( );
    }
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
        if ( it->natural_id != 0 && !it->touched && chunk->sidecars.player_edited ) {
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
    // Crowd separation runs after ordinary physics so every owned position is
    // current, and before mating so love pairs use the separated hitboxes.
    // Capture/frozen mode skips it wholesale to keep scripted poses exact.
    if ( !freeze ) {
        this->tick_crowding( world, player_pos, in, out );
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

// Every 2 s: while under the dimension's hostile mob_cap, try up to 4
// times to place one on a dark surface 24-44 blocks from the player.
void MobManager::tick_spawner( World &world, const glm::dvec3 &player_feet, const MobTickInput &in ) {
    if ( ++this->spawn_ticks < 2 * UPS_RATE ) {
        return;
    }
    const DimensionId player_dim = dimension_of_pos( player_feet );
    const DimensionProfile *profile = dimension_profile( player_dim );
    if ( !profile->natural_spawns ) {
        return;
    }
    // The spawn table is per-dimension: types opt in with a spawn_dims bit
    // and a night_spawn_weight — zombie .4 / skeleton .3 / creeper .3 in
    // the overworld, zombified piglin .7 / wither skeleton 1.0 in the nether.
    double weight_total = 0.0;
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        const MobTypeDef &def = mob_types[ t ];
        if ( def.night_spawn_weight > 0.0f && ( def.spawn_dims & ( 1u << player_dim ) ) ) {
            weight_total += def.night_spawn_weight;
        }
    }
    if ( weight_total <= 0.0 ) {
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
    if ( live >= profile->mob_cap ) {
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
        MobSpawnDiag &diag = this->spawn_diag;
        diag.attempts++;
        const double angle = angle_pick( this->rng );
        const double dist = dist_pick( this->rng );
        const double sx = player_feet.x + sin( angle ) * dist;
        const double sz = player_feet.z + cos( angle ) * dist;
        const int bx = static_cast<int>( floor( sx ) );
        const int bz = static_cast<int>( floor( sz ) );
        int top = -1;
        bool top_found = false;
        // Scan the column down to the despawn distance: deeper results would
        // be culled next tick anyway (>64 blocks). Nether caverns are tall —
        // a hovering player's ring columns often dip well past 24 below.
        for ( int y = player_y + 16; y > player_y - 63; y-- ) {
            if ( mob_terrain::solid_at( world, glm::ivec3( bx, y, bz ) ) ) {
                top = y;
                top_found = true;
                break;
            }
        }
        // Needs a floor with two standable cells above for the mob —
        // non-colliding and not liquid (no seabed/lava-floor spawns). The
        // found flag is required: the nether band sits at negative world y,
        // so a top<0 sentinel would veto every nether floor.
        if ( !top_found ) {
            diag.no_floor++;
            continue;
        }
        if ( !mob_terrain::spawnable_air_at( world, glm::ivec3( bx, top + 1, bz ) ) || !mob_terrain::spawnable_air_at( world, glm::ivec3( bx, top + 2, bz ) ) ) {
            diag.no_headroom++;
            continue;
        }
        const glm::dvec3 feet_pos( sx, top + 1.0, sz );
        const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( feet_pos / csize ) );
        const Chunk *chunk = world.chunkLoader.get_chunk( feet_chunk );
        if ( chunk == nullptr || chunk->load_state != CHUNK_LIVE ) {
            diag.not_live++;
            continue;
        }
        // Multiplayer: the night spawner only places into chunks we own.
        if ( this->mp_mode && ( this->net == nullptr || !this->net->owns_chunk( feet_chunk ) ) ) {
            diag.not_owned++;
            continue;
        }
        const glm::vec2 light = world.chunkLoader.light_sample( glm::vec3( feet_pos ) + glm::vec3( 0.0f, 0.5f, 0.0f ) );
        if ( !hostile_spawn_light_ok( light.x, light.y, daylight ) ) {
            diag.too_bright++;
            continue;
        }
        const double roll = type_roll( this->rng ) * weight_total;
        double weight_acc = 0.0;
        MobType type = MOB_TYPE_COUNT;
        for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
            const MobTypeDef &def = mob_types[ t ];
            if ( def.night_spawn_weight <= 0.0f || !( def.spawn_dims & ( 1u << player_dim ) ) ) {
                continue;
            }
            weight_acc += def.night_spawn_weight;
            if ( roll <= weight_acc ) {
                type = static_cast<MobType>( t );
                break;
            }
        }
        if ( type == MOB_TYPE_COUNT ) {
            continue;
        }
        // A non-empty spawn_on list whitelists the floor here too — the
        // wither skeleton's nether-brick list keeps it on fortress floors.
        // Nether/end picks only: the enderman's warped-nylium/end-stone
        // list must not veto its overworld night spawns, which take any
        // dark surface.
        if ( ( player_dim == DIM_NETHER || player_dim == DIM_END ) &&
             !mob_spawn_floor_ok( mob_types[ type ], world.get_loaded_block( glm::ivec3( bx, top, bz ) ).id ) ) {
            diag.bad_floor++;
            continue;
        }
        // Taller types need more than the generic two standable cells the
        // pre-roll check cleared: the 4-block ghast can't materialize with
        // its head in a cavern ceiling.
        const int headroom = static_cast<int>( ceil( mob_types[ type ].hitbox_h ) );
        bool headroom_ok = true;
        for ( int i = 1; i <= headroom; i++ ) {
            if ( !mob_terrain::spawnable_air_at( world, glm::ivec3( bx, top + i, bz ) ) ) {
                headroom_ok = false;
                break;
            }
        }
        if ( !headroom_ok ) {
            diag.no_headroom++;
            continue;
        }
        if ( this->spawn_any( type, feet_pos, yaw_pick( this->rng ), 0 ) ) {
            Mob &mob = this->mobs.back( );
            mob.despawnable = 1;
            this->announce_spawn( mob );
            diag.spawned++;
            live++;
            if ( live >= profile->mob_cap ) {
                return;
            }
        }
    }
}
