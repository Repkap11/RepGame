#ifndef HEADER_PLAYER_BLOCK_PLACED_EVENT_H
#define HEADER_PLAYER_BLOCK_PLACED_EVENT_H

#include "common/BlockUpdateQueue.hpp"

class PlayerBlockPlacedEvent : public BlockUpdateEvent {
  public:
    PlayerBlockPlacedEvent( int place, int block_x, int block_y, int block_z, BlockState blockState );
    void performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) override;

  private:
    int place;
    int block_x;
    int block_y;
    int block_z;
    BlockState blockState;
};

#endif