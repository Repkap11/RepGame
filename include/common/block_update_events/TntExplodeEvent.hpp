#pragma once

#include "entt/entity/fwd.hpp"

#include "common/BlockUpdateQueue.hpp"

// Fuse for a redstone-ignited TNT (UPS_RATE ticks/second — MC is 4s at 20tps).
#define TNT_FUSE_TICKS ( 2 * UPS_RATE )
// Chain-ignited TNT (launched by a blast) detonates with a shorter fuse —
// 0.5–1.5 s here, matching MC's 10–30 tick range at 20 tps.
#define TNT_CHAIN_FUSE_MIN_TICKS ( UPS_RATE / 2 )
#define TNT_CHAIN_FUSE_SPREAD_TICKS ( UPS_RATE )
#define TNT_BLAST_RADIUS 4

// Removes the block at center's cell (only if it is still TNT) and launches a
// primed TNT entity from it. Used by the redstone ignite path in
// PlayerBlockPlacedEvent; a no-op when a stale/racing trigger lands on a cell
// that is no longer TNT.
void tnt_ignite( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState, const glm::vec3 &center, const glm::vec3 &velocity, long fuse_ticks );

// Launches a primed (flying + flashing) TNT entity from center — the source
// block must already be out of the world. Marches the ballistic path against
// live blocks to predict the landing cell, then queues the TntExplodeEvent
// there at tick_number + fuse_ticks. TntExplodeEvent uses this directly for
// chain-ignited TNT after removing every in-radius TNT block, so landing
// prediction can't collide with blocks that are also exploding.
void tnt_launch( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState, const glm::vec3 &center, const glm::vec3 &velocity, long fuse_ticks );

// Detonates a primed TNT in three passes: removes every TNT block in the
// blast sphere (they all chain-ignite), destroys the remaining breakable
// blocks (hardness >= 0, fluids and unloaded cells untouched) spawning break
// debris, then launches each collected TNT away from the center with an
// upward kick and a shorter fuse. Queued by tnt_launch.
class TntExplodeEvent : public BlockUpdateEvent {
  public:
    TntExplodeEvent( long tick_number, const glm::ivec3 &pos, entt::entity primed_entity );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    // The primed entity whose fuse this event is — despawned here so the
    // flashing cube and the blast happen on the same tick.
    entt::entity primed_entity;
};
