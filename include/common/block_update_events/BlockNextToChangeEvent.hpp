#pragma once


#include "common/BlockUpdateQueue.hpp"

// Returns false if a block with the given state couldn't survive at block_pos
// (no required solid face/attachment).
bool block_can_survive_at( World &world, const glm::ivec3 &block_pos, const BlockState &block_state );
// The generic needs_place_on_any_solid part of block_can_survive_at —
// exported for the tall-plant lower placement test (partner not yet placed).
bool block_supports_present( World &world, const glm::ivec3 &block_pos, const BlockState &block_state );
// The piston base a head at block_pos is attached to — the cell behind it
// holds a piston still pointing at the head — or block_pos itself for an
// orphaned head. Shared by the survive check and the pick/mine retarget.
glm::ivec3 piston_head_base( World &world, const glm::ivec3 &block_pos, const BlockState &block_state );

class BlockNextToChangeEvent : public BlockUpdateEvent {
  public:
    BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    glm::ivec3 affecting_block_pos;
};