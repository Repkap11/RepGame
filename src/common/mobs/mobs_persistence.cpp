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

// Called from World::render for every chunk that finished load+finalize this
// frame. Claims ownership (so the leave path knows whether to persist) and
// spawns the worker-side pending_mobs as live mobs.
void MobManager::adopt_chunk( World &world, Chunk &chunk ) {
    // Multiplayer: mob files live server-side; the roll runs on CHUNK_ROLE
    // OWNER instead, so local pending_mobs are discarded.
    if ( !this->enabled || this->test_mode || this->mp_mode || this->stress.enabled( ) ) {
        chunk.sidecars.mobs.clear( );
        chunk.sidecars.mobs_removed.clear( );
        return;
    }
    for ( const OwnedChunk &owned : this->owned_chunks ) {
        if ( owned.pos == chunk.chunk_pos ) {
            chunk.sidecars.mobs.clear( );
            chunk.sidecars.mobs_removed.clear( );
            return;
        }
    }
    for ( const MobFileRecord &record : chunk.sidecars.mobs ) {
        // A NATURAL record whose type no longer rolls (legacy zombified
        // piglins / wither skeletons from before hostiles left the roll)
        // is dropped: hostile mobs are never persisted under the current
        // model. Player-caused spawns (eggs, bred babies) carry no NATURAL
        // flag and still restore.
        if ( ( record.flags & NET_MOB_FLAG_NATURAL ) && record.type < MOB_TYPE_COUNT && mob_types[ record.type ].spawn_chance <= 0.0f ) {
            continue;
        }
        // Uncapped: persisted (touched) mobs must never be dropped, and
        // fresh roll products are seed-bounded rather than population-capped
        // — naturals are exempt from MAX_MOBS entirely.
        if ( this->spawn( static_cast<MobType>( record.type ), glm::dvec3( record.x, record.y, record.z ), record.yaw, 1, false ) ) {
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
    this->owned_chunks.push_back( { chunk.chunk_pos, chunk.sidecars.mobs_file_present,
                                    std::set<uint8_t>( chunk.sidecars.mobs_removed.begin( ), chunk.sidecars.mobs_removed.end( ) ) } );
    chunk.sidecars.mobs.clear( );
    chunk.sidecars.mobs_removed.clear( );
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
    this->storage->batch_begin( ); // every owned chunk in one commit
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
    this->storage->batch_end( );
}
