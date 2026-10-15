#include <stdlib.h>
#include <math.h>

#include <vector>

#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

TntExplodeEvent::TntExplodeEvent( long tick_number, const glm::ivec3 &pos, const entt::entity primed_entity ) : BlockUpdateEvent( tick_number ), block_pos( pos ), primed_entity( primed_entity ) {
    this->name = "TntExplodeEvent";
}

// Matches TNT_GRAVITY in primed_tnt_vertex.glsl — the CPU predicts the same
// ballistic path the GPU renders.
#define TNT_GRAVITY 9.8f
// Seconds between ground-impact samples along the launch path.
#define TNT_PATH_STEP_S 0.04f
// Launch velocity ranges (blocks/sec) for chain-ignited TNT.
#define TNT_CHAIN_VEL_MIN 6.0f
#define TNT_CHAIN_VEL_SPREAD 5.0f
#define TNT_CHAIN_VEL_UP_MIN 4.5f
#define TNT_CHAIN_VEL_UP_SPREAD 4.0f
// Debris per destroyed block — less than DEBRIS_PER_BLOCK since a blast
// destroys hundreds of blocks in one tick.
#define DEBRIS_PER_EXPLODED_BLOCK 12

static float frand( ) {
    return static_cast<float>( rand( ) ) / static_cast<float>( RAND_MAX );
}

static glm::vec3 ballistic( const glm::vec3 &p0, const glm::vec3 &v, const float t ) {
    return p0 + v * t - glm::vec3( 0.0f, 0.5f * TNT_GRAVITY * t * t, 0.0f );
}

static bool path_hits_ground( World &world, const glm::vec3 &center, const glm::vec3 &velocity, const float t ) {
    // The cube's bottom face enters a solid cell => the entity rests there.
    const glm::vec3 p = ballistic( center, velocity, t );
    const glm::ivec3 under_cell = glm::ivec3( glm::floor( p - glm::vec3( 0.0f, 0.5f, 0.0f ) ) );
    const BlockState under = world.get_loaded_block( under_cell );
    return under.id != LAST_BLOCK_ID && block_definition_get_definition( under.id )->collides_with_player;
}

void tnt_launch( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState, const glm::vec3 &center, const glm::vec3 &velocity, const long fuse_ticks ) {
    World &world = repGameState.world;
    const float fuse_s = static_cast<float>( fuse_ticks ) / static_cast<float>( UPS_RATE );

    // March the ballistic path to find when the cube's bottom face enters a
    // solid cell; it lands there and detonates at that cell. Unloaded chunks
    // stop the prediction — the blast fires on schedule at the last free
    // position extrapolated to the full fuse.
    float ground_t = fuse_s;
    float t = TNT_PATH_STEP_S;
    for ( ; t < fuse_s; t += TNT_PATH_STEP_S ) {
        const glm::vec3 p = ballistic( center, velocity, t );
        const glm::ivec3 under_cell = glm::ivec3( glm::floor( p - glm::vec3( 0.0f, 0.5f, 0.0f ) ) );
        const BlockState under = world.get_loaded_block( under_cell );
        if ( under.id == LAST_BLOCK_ID ) {
            break;
        }
        if ( block_definition_get_definition( under.id )->collides_with_player ) {
            // Refine the impact time so the resting pose doesn't sink into
            // the floor by a whole step of fall speed.
            float lo = t - TNT_PATH_STEP_S, hi = t;
            for ( int i = 0; i < 5; i++ ) {
                const float mid = 0.5f * ( lo + hi );
                if ( path_hits_ground( world, center, velocity, mid ) ) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            ground_t = hi;
            break;
        }
    }
    const glm::ivec3 explode_pos = glm::ivec3( glm::floor( ballistic( center, velocity, ground_t ) ) );
    const entt::entity primed = world.spawn_primed_tnt( center, velocity, fuse_s, ground_t );
    blockUpdateQueue.addBlockUpdate( std::make_shared<TntExplodeEvent>( repGameState.tick_number + fuse_ticks, explode_pos, primed ) );
}

void tnt_ignite( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState, const glm::vec3 &center, const glm::vec3 &velocity, const long fuse_ticks ) {
    World &world = repGameState.world;
    // The block leaves the world immediately — the primed entity replaces it.
    // No break debris: igniting isn't breaking. If the cell is no longer TNT
    // a stale/racing ignite fired — drop it instead of spawning a phantom
    // explosion (and never clobber a different block).
    const glm::ivec3 cell = glm::ivec3( glm::floor( center ) );
    const BlockState cur = world.get_loaded_block( cell );
    if ( !block_definition_get_definition( cur.id )->is_tnt ) {
        return;
    }
    world.set_loaded_block( cell, BLOCK_STATE_AIR );
    repGameState.multiplayer.set_block( cell, BLOCK_STATE_AIR );
    queue_neighbor_block_updates( blockUpdateQueue, world, repGameState.tick_number, cell );
    tnt_launch( blockUpdateQueue, repGameState, center, velocity, fuse_ticks );
}

void TntExplodeEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    World &world = repGameState.world;
    // The flashing entity's fuse is up exactly when the blast runs — despawn
    // it here rather than relying on the wall-clock sweep in PrimedTnt::draw,
    // whose quantization could leave it visible for a few frames after the
    // blocks are gone.
    world.despawn_primed_tnt( this->primed_entity );
    const glm::vec3 center = glm::vec3( this->block_pos ) + 0.5f;
    std::vector<glm::ivec3> chained_tnt;

    // Pass 1: remove every TNT block in the blast sphere up front. They all
    // become primed entities — if they stayed solid while later launches
    // marched their landing paths, an entity could "land" on a TNT block
    // that is itself about to fly away and end up hovering on air.
    for ( int dx = -TNT_BLAST_RADIUS; dx <= TNT_BLAST_RADIUS; dx++ ) {
        for ( int dy = -TNT_BLAST_RADIUS; dy <= TNT_BLAST_RADIUS; dy++ ) {
            for ( int dz = -TNT_BLAST_RADIUS; dz <= TNT_BLAST_RADIUS; dz++ ) {
                if ( dx * dx + dy * dy + dz * dz > TNT_BLAST_RADIUS * TNT_BLAST_RADIUS ) {
                    continue;
                }
                const glm::ivec3 pos = this->block_pos + glm::ivec3( dx, dy, dz );
                const BlockState state = world.get_loaded_block( pos );
                if ( state.id == LAST_BLOCK_ID ) {
                    continue; // unloaded chunk — leave it alone
                }
                if ( block_definition_get_definition( state.id )->is_tnt ) {
                    world.set_loaded_block( pos, BLOCK_STATE_AIR );
                    repGameState.multiplayer.set_block( pos, BLOCK_STATE_AIR );
                    queue_neighbor_block_updates( blockUpdateQueue, world, this->tick_number, pos );
                    chained_tnt.push_back( pos );
                }
            }
        }
    }

    // Pass 2: destroy the breakable blocks. Unloaded cells, air, and fluids
    // are untouched — water/lava are fluids, not destroyable blocks.
    for ( int dx = -TNT_BLAST_RADIUS; dx <= TNT_BLAST_RADIUS; dx++ ) {
        for ( int dy = -TNT_BLAST_RADIUS; dy <= TNT_BLAST_RADIUS; dy++ ) {
            for ( int dz = -TNT_BLAST_RADIUS; dz <= TNT_BLAST_RADIUS; dz++ ) {
                if ( dx * dx + dy * dy + dz * dz > TNT_BLAST_RADIUS * TNT_BLAST_RADIUS ) {
                    continue;
                }
                const glm::ivec3 pos = this->block_pos + glm::ivec3( dx, dy, dz );
                const BlockState state = world.get_loaded_block( pos );
                if ( state.id == AIR || state.id == LAST_BLOCK_ID || state.id == WATER || state.id == LAVA ) {
                    continue;
                }
                if ( block_definition_get_definition( state.id )->hardness < 0.0f ) {
                    continue; // bedrock, barriers, portal frames survive
                }
                world.set_loaded_block( pos, BLOCK_STATE_AIR );
                repGameState.multiplayer.set_block( pos, BLOCK_STATE_AIR );
                world.spawn_block_debris( pos, state, AIR, DEBRIS_PER_EXPLODED_BLOCK );
                queue_neighbor_block_updates( blockUpdateQueue, world, this->tick_number, pos );
            }
        }
    }

    // Pass 3: launch the collected TNT. The world now holds only what
    // survives the blast, so the landing predictions see the final terrain —
    // they can no longer collide with blocks this explosion removed.
    for ( const glm::ivec3 &pos : chained_tnt ) {
        const glm::vec3 tnt_center = glm::vec3( pos ) + 0.5f;
        glm::vec3 away = tnt_center - center;
        away.y = 0.0f;
        glm::vec3 velocity;
        if ( const float hlen = glm::length( away ); hlen > 0.01f ) {
            velocity = away / hlen * ( TNT_CHAIN_VEL_MIN + frand( ) * TNT_CHAIN_VEL_SPREAD );
        } else {
            const float angle = frand( ) * 6.2832f;
            velocity = glm::vec3( cosf( angle ), 0.0f, sinf( angle ) ) * ( TNT_CHAIN_VEL_MIN + frand( ) * TNT_CHAIN_VEL_SPREAD );
        }
        velocity.y = TNT_CHAIN_VEL_UP_MIN + frand( ) * TNT_CHAIN_VEL_UP_SPREAD;
        const long fuse = TNT_CHAIN_FUSE_MIN_TICKS + rand( ) % TNT_CHAIN_FUSE_SPREAD_TICKS;
        tnt_launch( blockUpdateQueue, repGameState, tnt_center, velocity, fuse );
    }
}
