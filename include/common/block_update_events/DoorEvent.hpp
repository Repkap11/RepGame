#pragma once


#include "common/BlockUpdateQueue.hpp"

// Toggles a door pair open/closed (right-click) or syncs it to its combined
// redstone power (a power edge queued by PlayerBlockPlacedEvent). The event
// resolves the lower half from either cell, validates the pair, and rewrites
// both halves' rotations so a stale event is a harmless no-op.
class DoorEvent : public BlockUpdateEvent {
  public:
    typedef enum {
        TOGGLE,
        SYNC_POWER,
    } Mode;
    DoorEvent( long tick_number, const glm::ivec3 &pos, Mode mode );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
    Mode mode;
};
