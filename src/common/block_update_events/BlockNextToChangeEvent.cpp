#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, int x, int y, int z, int i, int j, int k )
    : BlockUpdateEvent( tick_number ), block_x( x + i ), block_y( y + j ), block_z( z + k ), affecting_block_x( x ), affecting_block_y( y ), affecting_block_z( z ) {
    this->name = "BlockNextToChangeEvent";
}

void BlockNextToChangeEvent::performAction( BlockUpdateQueue *blockUpdateQueue, World *world ) {
    // The updating block  was next to the updating one, and might need to change
    BlockState updateing_block_state = world_get_loaded_block( world, this->block_x, this->block_y, this->block_z );
    Block *updateing_block = block_definition_get_definition( updateing_block_state.id );

    // The affecting block is the one that origionally change
    BlockState affecting_block_state = world_get_loaded_block( world, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z );
    Block *affecting_block = block_definition_get_definition( affecting_block_state.id );

    // Water flow needs to start when a block next to water is broken
    if ( updateing_block->flows != 0 && affecting_block->breaks_in_liquid && this->affecting_block_y <= this->block_y ) {
        pr_debug( "Expanding a water x:%d y:%d z:%d into x:%d y:%d z:%d ", this->block_x, this->block_y, this->block_z, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z );
        BlockUpdateEvent *blockPlacedEvent =
            new PlayerBlockPlacedEvent( this->tick_number + updateing_block->flows, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z, {updateing_block_state.id, 0, updateing_block->initial_redstone_power} );
        blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
    }

    if ( updateing_block->breaks_in_liquid && affecting_block->flows != 0 && this->block_y <= this->affecting_block_y ) {
        pr_debug( "Expanding a water2 x:%d y:%d z:%d into x:%d y:%d z:%d ", this->affecting_block_x, this->affecting_block_y, this->affecting_block_z, this->block_x, this->block_y, this->block_z );
        BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( this->tick_number + affecting_block->flows, this->block_x, this->block_y, this->block_z, {affecting_block_state.id, 0, affecting_block->initial_redstone_power} );
        blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
    }

    // Make a placed lamp next to redstone turn on
    if ( affecting_block->affected_by_redstone_power && updateing_block_state.current_redstone_power > 0 ) {
        // redstone might change
        pr_debug( "Redstone might change" );
        BlockUpdateEvent *blockPlacedEvent = new PlayerBlockPlacedEvent( this->tick_number+10, this->affecting_block_x, this->affecting_block_y, this->affecting_block_z,
                                                                         {affecting_block_state.id, affecting_block_state.rotation, updateing_block_state.current_redstone_power - 1} );
        blockUpdateQueue->addBlockUpdate( blockPlacedEvent );
    }
}