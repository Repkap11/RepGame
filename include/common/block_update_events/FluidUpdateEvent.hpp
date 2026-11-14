#pragma once

#include "common/BlockUpdateQueue.hpp"

class World;

// Queues a FluidUpdateEvent for `pos` after the fluid's `flows` tick delay
// — or after 1 tick when a lava cell can already react with adjacent water,
// so obsidian/cobblestone feel instant. No-ops when the cell holds no fluid
// or an update is already pending (state.pending_fluid_updates dedupes).
// Declared here so tests can call it.
void fluid_schedule_update( RepGameState &state, const glm::ivec3 &pos );

// How many face-adjacent steps from pos+horiz_dir the nearest drop is: a
// passable cell (empty-or-fillable for `fluid`, or already holding it) whose
// below neighbor can receive a falling write. BFS bounded to `flow_slope`
// steps; returns flow_slope + 1 when no drop is in reach — the sentinel the
// spread direction pick compares against. Declared here so tests can call
// it.
int fluid_slope_distance( World &world, const glm::ivec3 &pos, const glm::ivec3 &horiz_dir, BlockID fluid, int flow_slope );

// One fluid cell's simulation step, scheduled by fluid_schedule_update when
// a face neighbor changed (BlockNextToChangeEvent) or a random tick finds a
// flow level that outlived its queue (chunk unload drops the events). On
// firing it recomputes the cell's level from its feeds — a source is
// stable, a falling cell stays level 1, a fed cell takes feed+1, an unfed
// one recedes to AIR — then spills down into a fallable cell and spreads
// sideways at level+1, preferring the directions whose slope search finds
// the closest drop. Lava cells touching water petrify (obsidian at a
// source, cobblestone flowing; water below a lava fall turns to stone), and
// water with two source neighbors over a solid/source base forms a new
// source. All writes ride the queue as PlayerBlockPlacedEvents — their
// neighbor wakes reschedule the touched fluid cells, so a flow advances
// one step per `flows` ticks and the queue drains when the shape settles.
class FluidUpdateEvent : public BlockUpdateEvent {
  public:
    FluidUpdateEvent( long tick_number, const glm::ivec3 &pos );
    void performAction( RepGameState &repGameState ) override;

  private:
    glm::ivec3 block_pos;
};
