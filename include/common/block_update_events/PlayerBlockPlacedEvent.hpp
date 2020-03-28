#ifndef HEADER_PLAYER_BLOCK_PLACED_EVENT_H
#define HEADER_PLAYER_BLOCK_PLACED_EVENT_H

#include "common/BlockUpdateQueue.hpp"

class PlayerBlockPlacedEvent : public BlockUpdateEvent {
  public:
    PlayerBlockPlacedEvent( long tick_number, int block_x, int block_y, int block_z, BlockState blockState, bool state_update );
    void performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) override;

  private:
    int block_x;
    int block_y;
    int block_z;
    BlockState blockState;
    bool state_update;

    void performActionToNeighbor( BlockUpdateQueue *blockUpdateQueue, World *world, int i, int j, int k );
};

#endif