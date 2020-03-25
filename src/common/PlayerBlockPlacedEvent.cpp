#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( int place, int x, int y, int z, BlockState blockState ) : place( place ), block_x( x ), block_y( y ), block_z( z ), blockState( blockState ) {
    this->name = "PlayerBlockPlacedEvent";
}

void PlayerBlockPlacedEvent::performAction( World *world ) {
    pr_debug( "Performing action:%s", this->name );
    multiplayer_set_block( this->place, this->block_x, this->block_y, this->block_z, this->blockState );
    world_set_loaded_block( world, TRIP_ARGS( this->block_ ), this->blockState );
}