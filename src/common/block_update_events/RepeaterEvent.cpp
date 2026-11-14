#include "common/block_update_events/RepeaterEvent.hpp"
#include "common/RepGame.hpp"
#include "common/repeater.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/schedule.hpp"

RepeaterEvent::RepeaterEvent( long tick_number, const glm::ivec3 &pos ) : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "RepeaterEvent";
}

void repeater_schedule_update( RepGameState &state, const glm::ivec3 &pos ) {
    const BlockState cell = state.world.get_loaded_block( pos );
    if ( cell.id == LAST_BLOCK_ID || !block_definition_get_definition( cell.id )->is_repeater ) {
        return;
    }
    // Nothing to do when the output already matches the input — a pending
    // tick is re-armed by RepeaterEvent itself, not here.
    if ( repeater_input_powered( state.world, pos, cell ) == ( cell.current_redstone_power > 0 ) ) {
        return;
    }
    if ( !state.pending_repeater_ticks.insert( pos ).second ) {
        return;
    }
    schedule_redstone_update<RepeaterEvent>( state, repeater_delay_gt( cell ), pos );
}

void RepeaterEvent::performAction( RepGameState &repGameState ) {
    repGameState.pending_repeater_ticks.erase( this->block_pos );
    World &world = repGameState.world;
    const BlockState cell = world.get_loaded_block( this->block_pos );
    if ( cell.id == LAST_BLOCK_ID || !block_definition_get_definition( cell.id )->is_repeater ) {
        return;
    }
    const bool powered = cell.current_redstone_power > 0;
    const bool input = repeater_input_powered( world, this->block_pos, cell );
    if ( powered && !input ) {
        BlockState off_state = cell;
        off_state.current_redstone_power = 0;
        schedule_redstone_update<PlayerBlockPlacedEvent>( repGameState, 0, this->block_pos, off_state, true );
    } else if ( !powered ) {
        // MC DiodeBlock.tick: an unpowered diode turns on even when the
        // input already went away, then re-arms for another delay — a
        // 1-tick input pulse still produces a full-delay output pulse.
        BlockState on_state = cell;
        on_state.current_redstone_power = REDSTONE_SOURCE_POWER;
        schedule_redstone_update<PlayerBlockPlacedEvent>( repGameState, 0, this->block_pos, on_state, true );
        if ( !input && repGameState.pending_repeater_ticks.insert( this->block_pos ).second ) {
            schedule_redstone_update<RepeaterEvent>( repGameState, repeater_delay_gt( cell ), this->block_pos );
        }
    }
}
