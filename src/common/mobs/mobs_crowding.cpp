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

void MobManager::tick_crowding( World &world, const glm::dvec3 &player_pos, const MobTickInput *in, MobTickOutput *out ) {
    PROFILE_SCOPE( "mobs_crowding" );
    if ( out != nullptr ) {
        out->player_push = glm::dvec3( 0.0 );
    }
    auto footprint_cells = []( const CrowdAgent &a, int *x0, int *x1, int *z0, int *z1 ) {
        *x0 = static_cast<int>( floor( a.min.x ) );
        *x1 = static_cast<int>( floor( a.max.x - MOB_CROWD_EPS ) );
        *z0 = static_cast<int>( floor( a.min.z ) );
        *z1 = static_cast<int>( floor( a.max.z - MOB_CROWD_EPS ) );
    };
    std::vector<CrowdAgent> &agents = this->crowd_agents;
    agents.clear( );
    agents.reserve( this->mobs.size( ) + 1 );
    int locally_applied = 0;
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( size_t i = 0; i < this->mobs.size( ); i++ ) {
        const Mob &mob = this->mobs[ i ];
        if ( mob.death_ticks > 0 || mob.seated || !mob.crowd_collides ) {
            continue;
        }
        const MobTypeDef &def = mob_types[ mob.type ];
        const double scale = scale_of( mob );
        const double width = def.hitbox_w * scale;
        const double height = def.hitbox_h * scale;
        const double half = width * 0.5;
        const bool external = mob.uid != 0 || mob.net.mirror;
        bool chunk_live = true;
        if ( !external ) {
            const glm::ivec3 feet_chunk = glm::ivec3( glm::floor( mob.pos / csize ) );
            const Chunk *chunk = world.chunkLoader.get_chunk( feet_chunk );
            chunk_live = chunk != nullptr && chunk->load_state == CHUNK_LIVE;
        }
        const bool apply_here = !external && !mob.no_ai && chunk_live;
        locally_applied += apply_here ? 1 : 0;
        // Mirrors/avatars represent a live entity whose owner applies the
        // other half; frozen/no-ai locals are immovable constraints.
        const bool resolvable = external || apply_here;
        CrowdAgent agent = { glm::dvec3( mob.pos.x - half, mob.pos.y, mob.pos.z - half ),
                             glm::dvec3( mob.pos.x + half, mob.pos.y + height, mob.pos.z + half ),
                             width, height, static_cast<int>( i ), resolvable, apply_here, 0, 0, 0, 0, 0 };
        footprint_cells( agent, &agent.x0, &agent.x1, &agent.z0, &agent.z1 );
        agents.push_back( agent );
    }
    if ( in != nullptr && in->player_pushable ) {
        const double width = PLAYER_WIDTH;
        const double height = in->player_height > 0.0f ? in->player_height : PLAYER_HEIGHT;
        const double half = width * 0.5;
        const glm::dvec3 feet = player_pos - glm::dvec3( 0.0, in->player_eye_height, 0.0 );
        CrowdAgent agent = { glm::dvec3( feet.x - half, feet.y, feet.z - half ),
                             glm::dvec3( feet.x + half, feet.y + height, feet.z + half ),
                             width, height, -1, out != nullptr, false, 0, 0, 0, 0, 0 };
        footprint_cells( agent, &agent.x0, &agent.x1, &agent.z0, &agent.z1 );
        agents.push_back( agent );
        locally_applied += out != nullptr ? 1 : 0;
    }
    if ( agents.size( ) < 2 || locally_applied == 0 ) {
        return;
    }

    // Only pairs involving an entity this client repositions can have a local
    // effect: constraints never propagate a push to a third entity here. Drop
    // constraints outside the local actors' union box before the broadphase so
    // a large frozen/mirrored population elsewhere doesn't cost insertion time.
    glm::dvec3 local_min( DBL_MAX );
    glm::dvec3 local_max( -DBL_MAX );
    for ( const CrowdAgent &agent : agents ) {
        const bool local_actor = agent.apply_here || agent.mob_index < 0;
        if ( !local_actor ) {
            continue;
        }
        local_min = glm::min( local_min, agent.min );
        local_max = glm::max( local_max, agent.max );
    }
    size_t kept = 0;
    for ( const CrowdAgent &agent : agents ) {
        const bool local_actor = agent.apply_here || agent.mob_index < 0;
        const bool near_local = agent.min.x < local_max.x && agent.max.x > local_min.x && agent.min.y < local_max.y && agent.max.y > local_min.y &&
                                agent.min.z < local_max.z && agent.max.z > local_min.z;
        if ( local_actor || near_local ) {
            agents[ kept++ ] = agent;
        }
    }
    agents.resize( kept );
    if ( agents.size( ) < 2 ) {
        return;
    }

    // Unit-cell x/z broadphase. Agents are inserted into every footprint
    // cell; pair_seen suppresses repeats when two wide footprints share
    // several cells, so each ordered pair is resolved once per tick.
    auto cell_key = []( const int x, const int z ) -> uint64_t {
        return ( static_cast<uint64_t>( static_cast<uint32_t>( x ) ) << 32 ) | static_cast<uint32_t>( z );
    };
    size_t insertion_count = 0;
    for ( const CrowdAgent &agent : agents ) {
        insertion_count += static_cast<size_t>( agent.x1 - agent.x0 + 1 ) * static_cast<size_t>( agent.z1 - agent.z0 + 1 );
    }
    std::vector<CrowdCellInsert> &inserts = this->crowd_inserts;
    inserts.clear( );
    inserts.reserve( insertion_count );
    for ( size_t i = 0; i < agents.size( ); i++ ) {
        agents[ i ].first_insert = inserts.size( );
        const CrowdAgent &agent = agents[ i ];
        for ( int x = agent.x0; x <= agent.x1; x++ ) {
            for ( int z = agent.z0; z <= agent.z1; z++ ) {
                inserts.push_back( { cell_key( x, z ), static_cast<int>( i ), -1, 0 } );
            }
        }
    }
    // Open-addressed cell table with linked insertion records. Epoch tags
    // reuse the table across ticks without clearing every bucket.
    size_t needed_size = 16;
    while ( needed_size < inserts.size( ) * 2 ) {
        needed_size <<= 1;
    }
    if ( this->crowd_cell_epochs.size( ) < needed_size ) {
        size_t new_size = this->crowd_cell_epochs.size( ) == 0 ? 16 : this->crowd_cell_epochs.size( );
        while ( new_size < needed_size ) {
            new_size <<= 1;
        }
        this->crowd_cell_keys.resize( new_size );
        this->crowd_cell_heads.resize( new_size );
        this->crowd_cell_epochs.resize( new_size, 0 );
    }
    const size_t table_mask = this->crowd_cell_epochs.size( ) - 1;
    if ( ++this->crowd_epoch == 0 ) {
        std::fill( this->crowd_cell_epochs.begin( ), this->crowd_cell_epochs.end( ), 0 );
        this->crowd_epoch = 1;
    }
    std::vector<uint64_t> &cell_keys = this->crowd_cell_keys;
    std::vector<int> &cell_heads = this->crowd_cell_heads;
    std::vector<uint32_t> &cell_epochs = this->crowd_cell_epochs;
    const uint32_t epoch = this->crowd_epoch;
    auto hash_cell = []( uint64_t key ) {
        key ^= key >> 30;
        key *= 0xbf58476d1ce4e5b9ULL;
        key ^= key >> 27;
        key *= 0x94d049bb133111ebULL;
        key ^= key >> 31;
        return key;
    };
    auto cell_slot = [&]( const uint64_t key ) {
        size_t slot = static_cast<size_t>( hash_cell( key ) ) & table_mask;
        while ( cell_epochs[ slot ] == epoch && cell_keys[ slot ] != key ) {
            slot = ( slot + 1 ) & table_mask;
        }
        return slot;
    };
    for ( size_t i = 0; i < inserts.size( ); i++ ) {
        const size_t slot = cell_slot( inserts[ i ].key );
        if ( cell_epochs[ slot ] != epoch ) {
            cell_epochs[ slot ] = epoch;
            cell_keys[ slot ] = inserts[ i ].key;
            cell_heads[ slot ] = -1;
        }
        inserts[ i ].slot = slot;
        inserts[ i ].next = cell_heads[ slot ];
        cell_heads[ slot ] = static_cast<int>( i );
    }

    std::vector<glm::dvec2> &pushes = this->crowd_pushes;
    pushes.clear( );
    pushes.resize( agents.size( ), glm::dvec2( 0.0 ) );
    std::vector<uint32_t> &pair_seen = this->crowd_pair_seen;
    pair_seen.clear( );
    pair_seen.resize( agents.size( ), 0 );
    for ( size_t i = 0; i < agents.size( ); i++ ) {
        const CrowdAgent &a = agents[ i ];
        size_t insert_index = a.first_insert;
        for ( int cell_x = a.x0; cell_x <= a.x1; cell_x++ ) {
            for ( int cell_z = a.z0; cell_z <= a.z1; cell_z++, insert_index++ ) {
                for ( int node = cell_heads[ inserts[ insert_index ].slot ]; node >= 0; node = inserts[ node ].next ) {
                    const int j = inserts[ node ].agent;
                    if ( j <= static_cast<int>( i ) ) {
                        continue;
                    }
                    const uint32_t pair_mark = static_cast<uint32_t>( i + 1 );
                    if ( pair_seen[ j ] == pair_mark ) {
                        continue;
                    }
                    pair_seen[ j ] = pair_mark;
                    const CrowdAgent &b = agents[ j ];
                    if ( a.min.y >= b.max.y || a.max.y <= b.min.y ) {
                        continue;
                    }
                    const double min_x = fmax( a.min.x, b.min.x );
                    const double max_x = fmin( a.max.x, b.max.x );
                    const double min_z = fmax( a.min.z, b.min.z );
                    const double max_z = fmin( a.max.z, b.max.z );
                    const double overlap_x = max_x - min_x;
                    const double overlap_z = max_z - min_z;
                    if ( overlap_x <= MOB_CROWD_EPS || overlap_z <= MOB_CROWD_EPS ) {
                        continue;
                    }

                    if ( !a.resolvable && !b.resolvable ) {
                        continue;
                    }

                    // Separate along the shallower horizontal penetration axis.
                    // Deterministic hash bits break exact-center ties without
                    // consuming the AI rng stream.
                    const uint32_t tie = static_cast<uint32_t>( i + 1 ) * 0x9e3779b1u ^ static_cast<uint32_t>( j + 1 ) * 0x85ebca6bu;
                    const bool use_x = overlap_x < overlap_z || ( overlap_x == overlap_z && ( tie & 1u ) == 0 );
                    const double acx = ( a.min.x + a.max.x ) * 0.5;
                    const double bcx = ( b.min.x + b.max.x ) * 0.5;
                    const double acz = ( a.min.z + a.max.z ) * 0.5;
                    const double bcz = ( b.min.z + b.max.z ) * 0.5;
                    const double center_delta = use_x ? bcx - acx : bcz - acz;
                    int sign;
                    if ( center_delta > 0.0 ) {
                        sign = 1;
                    } else if ( center_delta < 0.0 ) {
                        sign = -1;
                    } else {
                        sign = ( tie & 2u ) != 0 ? 1 : -1;
                    }
                    glm::dvec2 dir( 0.0 );
                    if ( use_x ) {
                        dir.x = static_cast<double>( sign );
                    } else {
                        dir.y = static_cast<double>( sign );
                    }
                    const double overlap = fmin( overlap_x, overlap_z );
                    const double amount = fmin( overlap * MOB_CROWD_RESPONSE, MOB_CROWD_PAIR_MAX );
                    const bool both = a.resolvable && b.resolvable;
                    const double share_a = a.resolvable ? ( both ? 0.5 : 1.0 ) : 0.0;
                    const double share_b = b.resolvable ? ( both ? 0.5 : 1.0 ) : 0.0;
                    pushes[ i ] -= dir * ( amount * share_a );
                    pushes[ j ] += dir * ( amount * share_b );
                }
            }
        }
    }

    static const std::vector<BodyBox> no_extra;
    for ( size_t i = 0; i < agents.size( ); i++ ) {
        glm::dvec2 push = pushes[ i ];
        const double len = glm::length( push );
        if ( len <= 1e-12 ) {
            continue;
        }
        if ( len > MOB_CROWD_TOTAL_MAX ) {
            push *= MOB_CROWD_TOTAL_MAX / len;
        }
        const CrowdAgent &agent = agents[ i ];
        glm::dvec3 feet;
        if ( agent.mob_index >= 0 ) {
            if ( !agent.apply_here ) {
                continue;
            }
            feet = this->mobs[ agent.mob_index ].pos;
        } else {
            if ( out == nullptr ) {
                continue;
            }
            feet = player_pos - glm::dvec3( 0.0, in->player_eye_height, 0.0 );
        }
        const double half = agent.width * 0.5;
        glm::dvec3 min( feet.x - half, feet.y, feet.z - half );
        glm::dvec3 max( feet.x + half, feet.y + agent.height, feet.z + half );
        const glm::dvec3 old_min = min;
        bool hit_axis[ 3 ];
        int hit_extra_axis[ 3 ];
        bool standing = false;
        Collision::move_box( world, min, max, glm::dvec3( push.x, 0.0, push.y ), no_extra, hit_axis, hit_extra_axis, &standing );
        const glm::dvec3 applied = min - old_min;
        if ( agent.mob_index >= 0 ) {
            Mob &mob = this->mobs[ agent.mob_index ];
            mob.pos.x += applied.x;
            mob.pos.z += applied.z;
        } else {
            out->player_push = glm::dvec3( applied.x, 0.0, applied.z );
        }
    }
}
