#pragma once


#include "common/BlockUpdateQueue.hpp"

// Applies a piston's extend/retract transition after its stored redstone power
// crosses the zero boundary. Queued by PlayerBlockPlacedEvent when a piston's
// power state changes; reads the live block state at fire time so stale or
// duplicate events are harmless no-ops.
class PistonEvent : public BlockUpdateEvent {
  public:
    PistonEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};
