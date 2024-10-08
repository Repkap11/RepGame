#pragma once


#include "common/BlockUpdateQueue.hpp"

class BlockNextToChangeEvent : public BlockUpdateEvent {
  public:
    BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    glm::ivec3 affecting_block_pos;
};