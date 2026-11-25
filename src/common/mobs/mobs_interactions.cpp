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

void MobManager::drop_held_parts( Mob &mob ) {
    for ( entt::entity e : mob.held_parts ) {
        if ( mob.held_cube ) {
            this->render_held_cube.remove( e );
        } else {
            this->render_held_sprite.remove( e );
        }
    }
    mob.held_parts.clear( );
    mob.held_drawn = LAST_BLOCK_ID;
}

std::vector<MobManager::Mob>::iterator MobManager::remove_mob( std::vector<Mob>::iterator it ) {
    this->drop_held_parts( *it );
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
// selected block only when its entry distance is nearer than the pick
// ray's hit on the block's real pick shape (block_dist — not the cell
// boundary; thin shapes would otherwise shadow an entity in their cell).
int MobManager::resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                                double block_dist, double *out_dist ) const {
    if ( !this->enabled ) {
        return -1;
    }
    double mob_dist = 0.0;
    const int mob = this->raycast( origin, dir, max_dist, &mob_dist );
    if ( mob < 0 || mob_dist >= block_dist ) {
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
bool MobManager::hurt( int index, float damage, const glm::dvec3 &attacker_pos, const glm::dvec3 &listener_pos ) {
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
    const double dist = glm::distance( mob.pos, listener_pos );
    const float gain = audio_distance_gain( dist );
    if ( mob.health <= 0.0f ) {
        // A dying mob freezes where it stood and tips over — the knockback
        // hop below is never integrated (tick_mob returns early once
        // death_ticks is set). Any residual vel would stream to mirrors,
        // whose dead reckoning would then ride the corpse up or sideways
        // between corrections; a dead mob's honest velocity is zero.
        mob.vel = glm::dvec3( 0.0 );
        mob.kb = glm::dvec3( 0.0 );
        mob.death_ticks = 1;
        if ( def.sound_death != NULL ) {
            audio_play_sfx_gain( def.sound_death, gain );
        }
        return true;
    }
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

bool MobManager::hostiles_within( const glm::dvec3 &pos, const double radius ) const {
    for ( const Mob &mob : this->mobs ) {
        if ( mob.death_ticks > 0 ) {
            continue;
        }
        const MobAI ai = mob_types[ mob.type ].ai;
        if ( ai != MOB_AI_HOSTILE_MELEE && ai != MOB_AI_HOSTILE_CREEPER && ai != MOB_AI_HOSTILE_RANGED && ai != MOB_AI_HOSTILE_BLAZE &&
             ai != MOB_AI_HOSTILE_GHAST ) {
            continue;
        }
        if ( glm::distance( mob.pos, pos ) <= radius ) {
            return true;
        }
    }
    return false;
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
    const float gain = audio_distance_gain( dist );
    audio_play_sfx_gain( "sheep_shear", gain );
    return true;
}

bool MobManager::is_sheared_at( const int index ) const {
    return index >= 0 && index < static_cast<int>( this->mobs.size( ) ) && this->mobs[ index ].sheared != 0;
}
