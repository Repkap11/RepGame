#pragma once


#include "common/BlockUpdateQueue.hpp"

// Queues BlockNextToChangeEvents for the 6 face neighbors of `pos`, plus the
// extended offsets dust needs for vertical connections (dust over/under solid
// and non-solid neighbors). Shared with PistonEvent, which moves several
// blocks per action.
void queue_neighbor_block_updates( BlockUpdateQueue &blockUpdateQueue, World &world, long tick_number, const glm::ivec3 &pos );

class PlayerBlockPlacedEvent : public BlockUpdateEvent {
  public:
    PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &block, BlockState blockState, bool state_update );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    BlockState blockState;
    bool state_update;
};