#ifndef HEADER_PLAYER_BLOCK_PLACED_EVENT_H
#define HEADER_PLAYER_BLOCK_PLACED_EVENT_H

#include "common/BlockUpdateQueue.hpp"

class PlayerBlockPlacedEvent : public BlockUpdateEvent {
  public:
    PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &block, BlockState blockState, bool state_update );
    void performAction( BlockUpdateQueue &blockUpdateQueue, World &world ) override;

  private:
    glm::ivec3 block_pos;
    BlockState blockState;
    bool state_update;

    void performActionToNeighbor( BlockUpdateQueue &blockUpdateQueue, World &world, const glm::ivec3 &offset);
};

#endif