#pragma once


#include "common/BlockUpdateQueue.hpp"

// Returns false if a block with the given state couldn't survive at block_pos
// (no required solid face/attachment).
bool block_can_survive_at( World &world, const glm::ivec3 &block_pos, const BlockState &block_state );

class BlockNextToChangeEvent : public BlockUpdateEvent {
  public:
    BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    glm::ivec3 affecting_block_pos;
};