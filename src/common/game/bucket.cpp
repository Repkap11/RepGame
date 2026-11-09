// repgame_use_bucket — right-click use of the bucket items. Reads the
// per-tick raycast results in state.block_selection (written by
// RepGame::update_block_selection) and queues the world change as a
// PlayerBlockPlacedEvent like any other player edit, so neighbour updates
// (flow recede/spread, lava reactions) happen through the sim.

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/fluid.hpp"
#include "common/world.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

// Survival item swap: consume one of the held stack, then put `result` back
// on the hotbar preferring the selected slot (the just-emptied slot takes
// it, matching MC's in-hand swap). Whatever doesn't fit is thrown ahead of
// the player like a manual drop so it isn't lost.
static void bucket_swap_item( RepGameState &state, const ItemID result ) {
    state.hotbar.consumeSelected( 1 );
    if ( !state.hotbar.addItemWithQuantity( result, 1, true ) ) {
        const glm::dvec3 look = glm::dvec3( state.camera.look );
        state.world.items.spawn( result, 1, state.camera.pos + look * 0.3, look * 0.045 + glm::dvec3( 0.0, 0.01, 0.0 ), 2 * UPS_RATE );
    }
    state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
}

static void bucket_finish( RepGameState &state, const BlockID fluid ) {
    state.input.click_delay_right = 30;
    audio_play_sfx( fluid == WATER ? "splash" : audio_block_dig_pool( fluid ) );
}

bool repgame_use_bucket( RepGameState &state ) {
    const ItemID held = state.hotbar.getSelectedItem( );
    if ( !item_is_bucket( held ) ) {
        return false;
    }
    const BlockSelection &sel = state.block_selection;
    if ( held == BUCKET ) {
        // Scoop: only a plain fluid source — flowing cells aren't a source,
        // and a waterlogged cell (fluid_in_cell reports WATER for it) can't
        // be drained at all.
        if ( !sel.fluidSelectionInBounds ) {
            return false;
        }
        const BlockState cell = state.world.get_loaded_block( sel.pos_fluid );
        const BlockID fluid = fluid_in_cell( cell );
        if ( fluid == LAST_BLOCK_ID || cell.waterlogged || fluid_level( cell ) != 0 ) {
            return false;
        }
        // Removing the source queues neighbour updates that recede the flow
        // naturally — no explicit drain needed.
        state.blockUpdateQueue.addBlockUpdate(
            std::make_shared<PlayerBlockPlacedEvent>( state.tick_number, sel.pos_fluid, BLOCK_STATE_AIR, false ) );
        if ( state.game_mode == GameMode_Survival ) {
            bucket_swap_item( state, fluid == LAVA ? LAVA_BUCKET : WATER_BUCKET );
        }
        bucket_finish( state, fluid );
        return true;
    }
    // Pour: a flowing hit cell fills back to a source in place; any other
    // fluid hit pours into the face-adjacent cell, and a plain block click
    // pours at pos_create like a placed block.
    const BlockID fluid = item_bucket_fluid( held );
    glm::ivec3 target;
    if ( sel.fluidSelectionInBounds ) {
        const BlockState cell = state.world.get_loaded_block( sel.pos_fluid );
        target = fluid_level( cell ) > 0 ? sel.pos_fluid : sel.pos_fluid_create;
    } else if ( sel.selectionInBounds ) {
        target = sel.pos_create;
    } else {
        return false;
    }
    const BlockState write = fluid_state( fluid, 0 );
    if ( !player_placement_valid( state.world, target, write ) ) {
        return false;
    }
    // Water written onto a waterloggable cell waterlogs it via
    // block_state_normalize in Chunk::set_block.
    state.blockUpdateQueue.addBlockUpdate(
        std::make_shared<PlayerBlockPlacedEvent>( state.tick_number, target, write, false ) );
    if ( state.game_mode == GameMode_Survival ) {
        bucket_swap_item( state, BUCKET );
    }
    bucket_finish( state, fluid );
    return true;
}
