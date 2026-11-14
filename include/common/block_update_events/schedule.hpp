#pragma once

// The two clocks a BlockUpdateEvent can run on, and the explicit
// schedule helper for each. Events live on one of two queues:
// state.blockUpdateQueue ticks in physics ticks (UPS_RATE) and drains
// once per game tick; state.redstoneQueue ticks in MC game ticks
// (REDSTONE_UPS) and drains when the redstone clock reaches an event's
// tick. Every site picks its clock by name — and never schedules from
// `this->tick_number`, which is in whichever base queued the event and
// would silently mix the two. Include from .cpp files only.

#include <utility>

#include "common/constants.hpp"
#include "common/game/game_state.hpp"

// The current time on the redstone clock, in MC game ticks. A pure
// function of tick_number, so nothing extra is persisted or synced and
// every client derives the same clock from its own tick_number.
inline long redstone_tick_now( const RepGameState &state ) {
    return state.tick_number * REDSTONE_UPS / UPS_RATE;
}

// Physics clock (UPS_RATE): fires `delay` game ticks out; 0 = next tick.
template <typename E, typename... A>
E *schedule_block_update( RepGameState &state, long delay, A &&...args ) {
    return state.blockUpdateQueue.addBlockUpdate<E>( state.tick_number + delay, std::forward<A>( args )... );
}

// Redstone clock (REDSTONE_UPS, MC game ticks): fires once
// redstone_tick_now reaches now + delay_gt; 0 = the next game tick's
// drain.
template <typename E, typename... A>
E *schedule_redstone_update( RepGameState &state, long delay_gt, A &&...args ) {
    return state.redstoneQueue.addBlockUpdate<E>( redstone_tick_now( state ) + delay_gt, std::forward<A>( args )... );
}

// One game tick's queue drain — physics queue first, then the redstone
// queue at redstone_tick_now. RepGame::process_block_updates calls it
// after the random ticks; rep_tests call it to advance the simulation.
void repgame_drain_block_updates( RepGameState &state );
