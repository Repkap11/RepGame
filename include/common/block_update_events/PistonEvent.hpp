#pragma once


#include "common/BlockUpdateQueue.hpp"

// Starts a piston's extend/retract transition when its stored redstone power
// crosses the zero boundary. Queued by PlayerBlockPlacedEvent on the redstone
// clock at delay 0; lands any in-flight move of this piston first (MC's
// finish-on-reverse), then writes MOVING_PISTON cells and kinematic movers —
// the cells land through per-cell PistonLandEvents PISTON_MOVE_GT later.
class PistonEvent : public BlockUpdateEvent {
  public:
    PistonEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};

// Lands one touched cell of a piston move on the redstone clock: a
// MOVING_PISTON cell is rewritten to its carried state, a vacated cell relays
// its AIR — the move's start writes are local-only.
class PistonLandEvent : public BlockUpdateEvent {
  public:
    PistonLandEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};

// Lands every in-flight move belonging to the piston at `base_pos` —
// finish-on-reverse before a new edge, and the on-exit land-all.
void piston_finish_moves( RepGameState &state, const glm::ivec3 &base_pos );
// Single-cell landing: MOVING_PISTON to carried (side table or fallback
// encoding), then multiplayer relay and a physics-clock neighbour wake.
void piston_land_cell( RepGameState &state, const glm::ivec3 &pos );
