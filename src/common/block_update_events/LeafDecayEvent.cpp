#include <queue>
#include <unordered_map>

#include "common/block_update_events/LeafDecayEvent.hpp"
#include "common/RepGame.hpp"
#include "common/block.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/schedule.hpp"

static const glm::ivec3 leaf_decay_dirs[ 6 ] = {
    glm::ivec3( 0, 1, 0 ),  //
    glm::ivec3( 0, -1, 0 ), //
    glm::ivec3( 1, 0, 0 ),  //
    glm::ivec3( -1, 0, 0 ), //
    glm::ivec3( 0, 0, 1 ),  //
    glm::ivec3( 0, 0, -1 ), //
};

bool leaf_has_log_support( World &world, const glm::ivec3 &pos ) {
    std::unordered_map<glm::ivec3, int> dist;
    std::queue<glm::ivec3> pending;
    dist.emplace( pos, 0 );
    pending.push( pos );
    while ( !pending.empty( ) ) {
        const glm::ivec3 cur = pending.front( );
        pending.pop( );
        const int d = dist[ cur ];
        for ( const glm::ivec3 &dir : leaf_decay_dirs ) {
            const glm::ivec3 next = cur + dir;
            const BlockID next_id = world.get_loaded_block( next ).id;
            if ( next_id == LAST_BLOCK_ID ) {
                return true; // unloaded: the canopy may continue
            }
            const Block *next_block = block_definition_get_definition( next_id );
            if ( next_block->is_log ) {
                return true;
            }
            if ( d + 1 <= LEAF_DECAY_RANGE && ( block_is_natural_leaf( next_block ) || block_is_persistent_leaf( next_block ) ) &&
                 dist.emplace( next, d + 1 ).second ) {
                pending.push( next );
            }
        }
    }
    return false;
}

LeafDecayEvent::LeafDecayEvent( long tick_number, const glm::ivec3 &pos ) //
    : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "LeafDecayEvent";
}

void LeafDecayEvent::performAction( RepGameState &repGameState ) {
    repGameState.pending_leaf_decay.erase( this->block_pos );
    const BlockState state = repGameState.world.get_loaded_block( this->block_pos );
    if ( state.id == LAST_BLOCK_ID || !block_is_natural_leaf( block_definition_get_definition( state.id ) ) ) {
        return;
    }
    if ( leaf_has_log_support( repGameState.world, this->block_pos ) ) {
        return;
    }
    // No log within LEAF_DECAY_RANGE: the leaf breaks and drops its rolled
    // items in either game mode (nothing mined it — decay is a natural
    // cause). Its neighbor updates queue the next leaves' checks.
    schedule_block_update<PlayerBlockPlacedEvent>( repGameState, 0, this->block_pos, BLOCK_STATE_AIR, false, BreakDrops::Always );
}
