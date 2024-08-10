#pragma once


#include "common/BlockUpdateQueue.hpp"

class PlayerBlockPlacedEvent : public BlockUpdateEvent {
  public:
    PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &block, BlockState blockState, bool state_update );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    BlockState blockState;
    bool state_update;

    void performActionToNeighbor( BlockUpdateQueue &blockUpdateQueue, World &world, const glm::ivec3 &offset);
};