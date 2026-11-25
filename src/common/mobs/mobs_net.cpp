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

const NetSync *MobManager::find_net_sync( const NetEntityId net_id ) {
    const Mob *mob = this->find_by_net_id( net_id );
    return mob == nullptr ? nullptr : &mob->net;
}

// Mirror of a remote-owned mob: no physics/AI/spawner/despawn — ease pos and
// yaw toward the last streamed targets, derive the gait from the applied
// horizontal motion, and let the flag-driven visuals (hurt flash, tip-over,
// creeper swell) play out. Ambient say/step sounds still run locally.
void MobManager::tick_mirror( Mob &mob, const glm::dvec3 &player_pos ) {
    mob.prev_pos = mob.pos;
    mob.prev_yaw = mob.yaw;
    mob.prev_sneak = mob.sneak;
    // Dead-reckon the streamed target forward between ENTITY_STATEs — the
    // owner only sends again when the sim diverges from this prediction.
    mob.net.pos += mob.net.vel;
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
    mob.anim_phase += 1.0f;
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
        audio_play_sfx_gain( def.sound_say, audio_distance_gain( dist ), 0.15f );
    }
    mob.step_acc += static_cast<float>( hd );
    if ( def.sound_step != NULL && mob.step_acc >= 0.7f ) {
        mob.step_acc = 0.0f;
        audio_play_sfx_gain( def.sound_step, def.step_gain * audio_distance_gain( dist ), 0.1f );
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
        // The corpse is frozen — don't dead-reckon it off the record's vel.
        mob.net.vel = glm::dvec3( 0.0 );
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
        existing->net.recv_pos = existing->net.pos;
        existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        existing->net_yaw = rec.yaw;
        existing->net.chunk = mob_net_chunk_of( existing->net.pos );
        return;
    }
    this->spawn_record( rec, mirror );
}

void MobManager::net_apply_state( const NetEntityStateEntry &e, const glm::dvec3 &listener_pos ) {
    if ( !this->enabled ) {
        return;
    }
    Mob *mob = this->find_by_net_id( e.id );
    if ( mob == nullptr ) {
        return;
    }
    const bool has_pos = ( e.present & ( NET_SE_POS_ABS | NET_SE_POS_DELTA ) ) != 0;
    if ( has_pos ) {
        // Track the chunk the server's copy sits in (same boundary
        // hysteresis the server applies); a predicted local spawn we don't
        // own flips to a mirror on its first authoritative state. recv_pos
        // is the delta baseline the next entry decodes against.
        mob->net.recv_pos = glm::dvec3( e.x, e.y, e.z );
        netsync_track_chunk( mob->net, mob->net.recv_pos );
    }
    const bool owned_here = this->net != nullptr && this->net->owns_chunk( mob->net.chunk );
    if ( !mob->net.mirror && owned_here ) {
        // Owned: no server-originated mob state (partial pickups are items
        // only); just keep the provenance flag bits in sync.
        if ( e.present & NET_SE_FLAGS ) {
            mob->net_flags = ( mob->net_flags & ~( NET_MOB_FLAG_DESPAWNABLE | NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED ) ) |
                             ( e.flags & ( NET_MOB_FLAG_DESPAWNABLE | NET_MOB_FLAG_NATURAL | NET_MOB_FLAG_TOUCHED ) );
        }
        return;
    }
    const uint32_t old_flags = mob->net_flags;
    mob->net.mirror = true;
    if ( has_pos ) {
        mob->net.pos = mob->net.recv_pos;
    }
    if ( e.present & NET_SE_VEL ) {
        mob->net.vel = glm::dvec3( e.vx, e.vy, e.vz );
    }
    if ( e.present & NET_SE_YAW ) {
        mob->net_yaw = e.yaw;
    }
    if ( e.present & NET_SE_COUNT ) {
        mob->variant = e.count;
    }
    if ( e.present & NET_SE_HEALTH ) {
        mob->health = e.health;
    }
    if ( !( e.present & NET_SE_FLAGS ) ) {
        return;
    }
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
    // mirrors replay it here faded against the listener distance.
    const float sfx_gain = audio_distance_gain( glm::distance( mob->pos, listener_pos ) );
    if ( ( e.flags & NET_MOB_FLAG_HURT ) && !( old_flags & NET_MOB_FLAG_HURT ) ) {
        mob->hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
        if ( def.sound_hurt != NULL ) {
            audio_play_sfx_gain( def.sound_hurt, sfx_gain );
        }
    }
    // LOVE rides the same edge: the owner's feed() opens its own heart
    // window, so this opens it on every other client.
    if ( ( e.flags & NET_MOB_FLAG_LOVE ) && !( old_flags & NET_MOB_FLAG_LOVE ) ) {
        mob->heart_ticks = HEART_TICKS;
    }
    // The snip is the SHEARED edge's sound — the owner's shear() already
    // played it for the shearer, mirrors replay it at the listener gain.
    if ( ( e.flags & NET_MOB_FLAG_SHEARED ) && !( old_flags & NET_MOB_FLAG_SHEARED ) ) {
        audio_play_sfx_gain( "sheep_shear", sfx_gain );
    }
    if ( ( e.flags & NET_MOB_FLAG_DYING ) && !( old_flags & NET_MOB_FLAG_DYING ) ) {
        mob->death_ticks = 1;
        // The corpse is frozen — stop dead-reckoning it even if this entry
        // (or a stale one still in flight) carried a nonzero vel.
        mob->net.vel = glm::dvec3( 0.0 );
        if ( def.sound_death != NULL ) {
            audio_play_sfx_gain( def.sound_death, sfx_gain );
        }
    }
    if ( e.flags & NET_MOB_FLAG_FUSE ) {
        if ( !( old_flags & NET_MOB_FLAG_FUSE ) ) {
            mob->fuse_ticks = 1;
            audio_play_sfx_gain( "fuse", sfx_gain );
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
        mob->net.recv_pos = mob->net.pos;
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
        const bool sheared = this->shear( index, static_cast<ItemID>( p.item ), state.camera.pos );
        mp_test::event( &state, "mob_action_shear", "\"id\":\"%llu\",\"item\":%u,\"sheared\":%d",
                        (unsigned long long)p.id, p.item, (int)sheared );
        if ( sheared ) {
            RepGame::spawn_mob_shear_drop( state, mob_types[ this->mobs[ index ].type ], this->mobs[ index ].pos );
        }
        return;
    }
    const bool killed = this->hurt( index, p.damage, glm::dvec3( p.ax, p.ay, p.az ), state.camera.pos );
    mp_test::event( &state, "mob_action_hit", "\"id\":\"%llu\",\"dmg\":%g,\"killed\":%d,\"survival\":%d",
                    (unsigned long long)p.id, p.damage, (int)killed, (int)p.attacker_survival );
    if ( killed && p.attacker_survival && !this->is_baby_at( index ) ) {
        RepGame::spawn_mob_drops( state, mob_types[ this->mobs[ index ].type ], this->mobs[ index ].pos );
    }
}

void MobManager::mirror_hit_feedback( const int index, const glm::dvec3 &listener_pos ) {
    if ( index < 0 || index >= static_cast<int>( this->mobs.size( ) ) ) {
        return;
    }
    Mob &mob = this->mobs[ index ];
    const MobTypeDef &def = mob_types[ mob.type ];
    mob.hurt_ticks = static_cast<int>( 0.3f * UPS_RATE );
    const double dist = glm::distance( mob.pos, listener_pos );
    const float gain = audio_distance_gain( dist );
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
    const uint32_t session = this->net != nullptr ? this->net->session( ) : 0;
    for ( auto it = this->mobs.begin( ); it != this->mobs.end( ); ) {
        if ( it->net.id != 0 && it->net.chunk == chunk && by_id.find( it->net.id ) == by_id.end( ) ) {
            // Our in-view spawns are always live server-side: absence from
            // the snapshot only means it predates the spawn, so keep them.
            // Stray spawns carry no id server-side — the wipe still applies
            // (a remapped record respawns in the same grant). Natural ids
            // have a 0xFF top byte and can never match a session.
            const bool ours = session != 0 && ( it->net.id >> 32 ) == session && !this->net->stray_spawn( it->net.id );
            if ( !keep_ids.count( it->net.id ) && !ours ) {
                it = this->remove_mob( it );
                continue;
            }
            // Kept past a stale snapshot: settle sim ownership on the role
            // (its mirror flag was guessed before the role was known).
            it->net.mirror = !owned;
        }
        ++it;
    }
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind != NetEntityKind::MOB ) {
            continue;
        }
        Mob *existing = this->find_by_net_id( rec.id );
        if ( existing != nullptr ) {
            existing->net.mirror = !owned;
            existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
            existing->net.recv_pos = existing->net.pos;
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
            live->sidecars.mobs.clear( );
            const std::set<uint8_t> skip( removed_natural.begin( ), removed_natural.end( ) );
            live->spawn_mobs( &skip );
            for ( const MobFileRecord &record : live->sidecars.mobs ) {
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
            live->sidecars.mobs.clear( );
            live->sidecars.mobs_spawned = 1;
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
        const bool keyframe = !mob.net.sent || netsync_keyframe_due( mob.net, mob.pos, tick_number );
        const bool crossing = mob.net.sent && netsync_chunk_changed( mob.net, mob.pos );
        const bool yaw_changed = fabsf( mob.yaw - mob.net_sent_yaw ) > 1e-4f;
        const bool health_changed = mob.health != mob.net_sent_health;
        const bool flags_changed = flags != mob.net_sent_flags;
        const bool drifted = netsync_drifted( mob.net, mob.pos, mob.vel, tick_number );
        const bool stale = netsync_stale( mob.net, mob.pos );
        // Emit-reason tallies under REPGAME_NET_WHY (per ~5 s).
        static const bool net_why = env_is_set( "REPGAME_NET_WHY" );
        static long why_last = -1;
        static long why_key = 0, why_drift = 0, why_vel = 0, why_yaw = 0, why_aux = 0, why_x = 0, why_stale = 0;
        if ( net_why ) {
            if ( why_last < 0 ) {
                why_last = tick_number;
            }
            if ( keyframe ) {
                why_key++;
            } else {
                if ( stale ) {
                    why_stale++;
                }
                if ( drifted ) {
                    const glm::dvec3 predicted =
                        mob.net.sent_pos + mob.net.sent_vel * static_cast<double>( tick_number - mob.net.sent_tick );
                    if ( glm::length( mob.pos - predicted ) > NET_STATE_DRIFT_EPS ) {
                        why_drift++;
                    }
                    if ( glm::length( mob.vel - mob.net.sent_vel ) > NET_STATE_VEL_EPS ) {
                        why_vel++;
                    }
                }
                if ( yaw_changed ) {
                    why_yaw++;
                }
                if ( health_changed || flags_changed ) {
                    why_aux++;
                }
                if ( crossing ) {
                    why_x++;
                }
            }
            if ( tick_number - why_last >= 5 * UPS_RATE ) {
                pr_debug( "net_why: key=%ld drift=%ld vel=%ld yaw=%ld aux=%ld cross=%ld stale=%ld", why_key, why_drift,
                          why_vel, why_yaw, why_aux, why_x, why_stale );
                why_key = why_drift = why_vel = why_yaw = why_aux = why_x = why_stale = 0;
                why_last = tick_number;
            }
        }
        // Receivers dead-reckon the streamed pos/vel forward, so emit only
        // when the sim diverges from that prediction, aux fields changed, a
        // chunk-boundary crossing would stale the server's belief, the
        // record pos fell >2 blocks behind (stale), or the move-gated
        // keyframe backstop is due.
        if ( !keyframe && !crossing && !drifted && !stale && //
             !yaw_changed && !health_changed && !flags_changed ) {
            continue;
        }
        NetEntityStateEntry e;
        e.id = mob.net.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::MOB );
        e.baseline = mob.net.sent_pos;
        e.present = static_cast<uint8_t>( ( keyframe ? NET_SE_POS_ABS : NET_SE_POS_DELTA ) | NET_SE_VEL );
        // Stream the value the receiver decodes (quantized) so the next
        // delta/dead-reckon builds on what it actually holds.
        const glm::dvec3 wire_pos = netsync_decoded_pos( mob.net, mob.pos, keyframe );
        e.x = wire_pos.x;
        e.y = wire_pos.y;
        e.z = wire_pos.z;
        const glm::dvec3 wire_vel = netsync_wire_vel( mob.vel );
        e.vx = static_cast<float>( wire_vel.x );
        e.vy = static_cast<float>( wire_vel.y );
        e.vz = static_cast<float>( wire_vel.z );
        if ( keyframe || yaw_changed ) {
            e.present |= NET_SE_YAW;
            e.yaw = mob.yaw;
        }
        if ( keyframe || health_changed ) {
            e.present |= NET_SE_HEALTH;
            e.health = mob.health;
        }
        if ( keyframe || flags_changed ) {
            e.present |= NET_SE_FLAGS;
            e.flags = flags;
        }
        if ( keyframe ) {
            e.present |= NET_SE_COUNT;
            e.count = mob.variant;
        }
        out.push_back( e );
        if ( e.present & NET_SE_YAW ) {
            mob.net_sent_yaw = mob.yaw;
        }
        if ( e.present & NET_SE_HEALTH ) {
            mob.net_sent_health = mob.health;
        }
        if ( e.present & NET_SE_FLAGS ) {
            mob.net_sent_flags = flags;
        }
        // The server now applies its boundary hysteresis to this pos.
        netsync_mark_sent( mob.net, wire_pos, wire_vel, tick_number, keyframe );
    }
}
