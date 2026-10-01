#pragma once


#include "common/BlockUpdateQueue.hpp"

// Re-checks whether the local player is standing on the pressure plate at
// block_pos. Presses/unpresses it via PlayerBlockPlacedEvent and reschedules
// itself while occupied, so the plate releases shortly after the player
// steps off.
class PressurePlateEvent : public BlockUpdateEvent {
  public:
    PressurePlateEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};
