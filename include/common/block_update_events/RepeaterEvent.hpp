#pragma once

#include "common/BlockUpdateQueue.hpp"

// Queues a RepeaterEvent for `pos` after the cell's own delay —
// 2 * delay game ticks. No-ops when the cell isn't a repeater,
// when the input already matches the output (nothing to change), or when
// an update is already pending (state.pending_repeater_ticks dedupes).
void repeater_schedule_update( RepGameState &state, const glm::ivec3 &pos );

// One repeater cell's delay tick, scheduled by repeater_schedule_update
// when a face neighbor changed and the input no longer matches the
// output. On firing it re-reads the cell (a broken or swapped block just
// returns) and applies MC's DiodeBlock.tick semantics through
// PlayerBlockPlacedEvent state updates: a powered repeater with no input
// turns off, an unpowered one turns on even if the input already went
// away — and in that case re-arms itself for another delay, which is
// what extends a shorter input pulse to the full delay.
class RepeaterEvent : public BlockUpdateEvent {
  public:
    RepeaterEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};
