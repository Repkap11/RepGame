#ifndef HEADER_BLOCK_NEXT_TO_CHANGED_EVENT_H
#define HEADER_BLOCK_NEXT_TO_CHANGED_EVENT_H

#include "common/BlockUpdateQueue.hpp"

class BlockNextToChangeEvent : public BlockUpdateEvent {
  public:
    BlockNextToChangeEvent( int x, int y, int z, int i, int j, int k );
    void performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) override;

  private:
    int block_x;
    int block_y;
    int block_z;
    int affecting_block_x;
    int affecting_block_y;
    int affecting_block_z;
};

#endif