#pragma once

#include "common/BlockUpdateQueue.hpp"

// Releases a falls_with_gravity block into a FALLING BlockBodies entity.
// Re-checks on firing that the cell still holds a gravity block and that the
// cell below is loaded and non-colliding (queued 2 ticks out by
// BlockNextToChangeEvent's support check, so the world may have changed).
// When it fires, the whole contiguous run of falling-type blocks above the
// cell releases on the same tick — each cell goes through set AIR,
// multiplayer.set_block, queue_neighbor_block_updates and spawn_falling. A
// spawn refused at the body cap leaves that block (and everything above it)
// in place.
class FallingBlockEvent : public BlockUpdateEvent {
  public:
    FallingBlockEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};
