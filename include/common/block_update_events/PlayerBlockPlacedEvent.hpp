#pragma once


#include "common/BlockUpdateQueue.hpp"

// Queues BlockNextToChangeEvents for the 6 face neighbors of `pos`, plus the
// extended offsets dust needs for vertical connections (dust over/under solid
// and non-solid neighbors). Shared with PistonEvent, which moves several
// blocks per action.
void queue_neighbor_block_updates( BlockUpdateQueue &blockUpdateQueue, World &world, long tick_number, const glm::ivec3 &pos );

// Mirrors the rejection checks PlayerBlockPlacedEvent::performAction applies
// to a non-state_update, non-AIR placement, so a caller can tell up front
// whether placing blockState at pos would be silently dropped.
bool player_placement_valid( World &world, const glm::ivec3 &pos, const BlockState &blockState );

// Whether a removal performed by this event spawns the broken block's drops:
// None never; Survival only in survival mode; Always in both game modes
// (natural causes like leaf decay don't care who removed the support).
enum class BreakDrops : uint8_t { None, Survival, Always };

class PlayerBlockPlacedEvent : public BlockUpdateEvent {
  public:
    PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &block, BlockState blockState, bool state_update, BreakDrops drops = BreakDrops::None );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    BlockState blockState;
    bool state_update;
    BreakDrops drops;
};