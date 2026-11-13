#include "common/break_drops.hpp"

// RepGame.hpp pulls in game_state.hpp itself — including game_state.hpp
// first re-enters RepGame.hpp mid-include and leaves RepGameState incomplete.
#include "common/RepGame.hpp"
#include "common/game/game_internal.hpp"
#include "common/block.hpp"
#include "common/world.hpp"

void break_drops( RepGameState &state, const glm::ivec3 &pos, const BlockState &broken_state, const ItemID force_item ) {
    const BlockID broken_id = broken_state.id;
    if ( broken_id == AIR || broken_id == LAST_BLOCK_ID ) {
        return;
    }
    const Block *broken = block_definition_get_definition( broken_id );
    if ( broken->is_door || block_is_tall_plant( broken ) ) {
        // A two-cell block yields one item no matter which half went first:
        // the half that lands while its partner still stands drops it, the
        // second kill sees an empty partner and drops nothing.
        const bool upper = broken->is_door ? broken->is_door_upper : broken->is_tall_plant_upper;
        const BlockID partner = broken->is_door ? broken->door_partner : broken->tall_plant_partner;
        const glm::ivec3 partner_pos = pos + ( upper ? glm::ivec3( 0, -1, 0 ) : glm::ivec3( 0, 1, 0 ) );
        if ( state.world.get_loaded_block( partner_pos ).id != partner ) {
            return;
        }
    }
    if ( broken->bed_partner != LAST_BLOCK_ID ) {
        // Same one-drop rule for the horizontal bed pair — the partner
        // direction comes off the broken half's yaw.
        const glm::ivec3 partner_pos = pos + block_bed_partner_dir( broken, broken_state.rotation );
        if ( state.world.get_loaded_block( partner_pos ).id != broken->bed_partner ) {
            return;
        }
    }
    static uint32_t drop_rng_state = 0x9e3779b9u;
    const glm::dvec3 drop_pos = glm::dvec3( pos ) + glm::dvec3( 0.5 );
    if ( force_item != LAST_BLOCK_ID ) {
        state.world.items.spawn( force_item, 1, drop_pos, repgame_item_drop_hop( ), 10 );
        return;
    }
    // A door's upper half drops the lower's item; persistent leaves drop
    // through their natural variant. Blocks with a "drops" table roll it;
    // the rest drop themselves (kept behind the pickable / render-order
    // gate). See block_roll_drops.
    ItemStack drops[ 8 ];
    const int num_drops = block_roll_drops( block_item_id( broken_id ), &drop_rng_state, drops, 8 );
    for ( int i = 0; i < num_drops; i++ ) {
        const ItemID drop_id = drops[ i ].item_id;
        if ( item_is_block( drop_id ) ) {
            const Block *blockDef = block_definition_get_definition( static_cast<BlockID>( drop_id ) );
            if ( !blockDef->is_pickable || blockDef->renderOrder == RenderOrder_Transparent || blockDef->renderOrder == RenderOrder_Water ) {
                continue;
            }
        }
        state.world.items.spawn( drop_id, drops[ i ].count, drop_pos, repgame_item_drop_hop( ), 10 );
    }
}
