// RepGame member functions: hotbar, chest/crafting/survival inventory UI logic.

#include <cmath>
#include <ctime>
#include <string>
#include <unistd.h>
#include <chrono>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/renderer/frame_buffer.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/utils/env_config.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/game/game_internal.hpp"

void RepGame::add_to_hotbar( const bool alsoSelect, const ItemID itemId ) {
    if ( globalGameState.hotbar.getSelectedItem( ) == itemId ) {
        // We don't support having the same item in the hotbar or inventory more than once, until there are qtys.
        return;
    }
    if ( globalGameState.hotbar.addItem( alsoSelect, itemId ) ) {
        // The item we added might have gone into our selected slot.
        const ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
        globalGameState.ui.overlay.set_holding_item( selectedBlock );
    }
}

// Moves every non-empty chest slot into the player inventories (survival
// first, hotbar for overflow). Items that fit nowhere vanish — there are no
// dropped-item entities, consistent with the inventory-close/drop paths.
void repgame_loot_chest_contents( RepGameState &state, const ChestContents &contents ) {
    for ( int i = 0; i < CHEST_SLOTS; i++ ) {
        const InventorySlot &slot = contents.slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            continue;
        }
        int leftover = state.survival_inventory.addItem( slot.item_id, slot.quantity );
        if ( leftover > 0 ) {
            const int hotbar_space = state.hotbar.spaceFor( slot.item_id );
            const int to_hotbar = leftover < hotbar_space ? leftover : hotbar_space;
            if ( to_hotbar > 0 ) {
                state.hotbar.addItemWithQuantity( slot.item_id, to_hotbar, false );
            }
        }
    }
    state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
}

void RepGame::open_chest( const glm::ivec3 &pos ) {
    if ( globalGameState.ui.chest.open && globalGameState.ui.chest.pos != pos ) {
        // Unsubscribes from the previous chest in multiplayer.
        this->close_chest( );
    }
    globalGameState.ui.chest.open = true;
    globalGameState.ui.chest.pos = pos;
    globalGameState.input.inventory_open = true;
    audio_play_sfx( "chest" );
    // An open cancels a pending loot snapshot for the same position.
    globalGameState.pending_chest_loot.erase( pos );
    if ( globalGameState.multiplayer.is_active( ) ) {
        globalGameState.multiplayer.chest_open( pos );
    }
    // Ensure a local entry exists (empty until CHEST_CONTENTS arrives in
    // multiplayer; the chest file contents in singleplayer) and repaint.
    const ChestContents *contents = globalGameState.world.get_or_create_chest_contents( pos );
    if ( contents != nullptr ) {
        for ( int i = 0; i < CHEST_SLOTS; i++ ) {
            globalGameState.ui.chest.inventory_renderer.changeSlotItem( i, contents->slots[ i ] );
        }
    }
}

void RepGame::close_chest( ) {
    if ( !globalGameState.ui.chest.open ) {
        return;
    }
    globalGameState.ui.chest.open = false;
    audio_play_sfx( "chest_close" );
    if ( globalGameState.multiplayer.is_active( ) ) {
        globalGameState.multiplayer.chest_close( globalGameState.ui.chest.pos );
    }
}

// --- Chest slot interaction helpers -----------------------------------------
// These mirror SurvivalInventory::pickupOrSwapSlot / rightClickSlot but run on
// ChestContents; every mutation funnels through chest_sync_slot so the chunk
// gets dirtied, the icon repaints, and the server hears about it.

static void chest_sync_slot( RepGameState &state, int slot_index, const InventorySlot &slot ) {
    state.world.set_chest_slot( state.ui.chest.pos, slot_index, slot );
    state.ui.chest.inventory_renderer.changeSlotItem( slot_index, slot );
    state.multiplayer.chest_set_slot( state.ui.chest.pos, static_cast<uint32_t>( slot_index ), slot );
}

static void chest_pickup_or_swap( RepGameState &state, int slot_index ) {
    ChestContents *contents = state.world.get_or_create_chest_contents( state.ui.chest.pos );
    if ( contents == nullptr ) {
        return;
    }
    InventorySlot slot = contents->slots[ slot_index ];
    InventorySlot &held = state.ui.held_slot;
    if ( !state.ui.holding_slot ) {
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return;
        }
        held = slot;
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        state.ui.holding_slot = true;
    } else if ( slot.item_id == held.item_id && slot.quantity > 0 && slot.quantity < item_max_stack( held.item_id ) ) {
        const int space = item_max_stack( held.item_id ) - slot.quantity;
        const int to_move = held.quantity < space ? held.quantity : space;
        slot.quantity += to_move;
        held.quantity -= to_move;
        if ( held.quantity <= 0 ) {
            held.item_id = LAST_BLOCK_ID;
            state.ui.holding_slot = false;
        }
    } else {
        const InventorySlot tmp = slot;
        slot = held;
        held = tmp;
        if ( held.item_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
            state.ui.holding_slot = false;
        }
    }
    chest_sync_slot( state, slot_index, slot );
}

static void chest_right_click( RepGameState &state, int slot_index ) {
    ChestContents *contents = state.world.get_or_create_chest_contents( state.ui.chest.pos );
    if ( contents == nullptr ) {
        return;
    }
    InventorySlot slot = contents->slots[ slot_index ];
    InventorySlot &held = state.ui.held_slot;
    if ( !state.ui.holding_slot ) {
        // Pick up half the stack, rounded up.
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return;
        }
        const int take = ( slot.quantity + 1 ) / 2;
        held.item_id = slot.item_id;
        held.quantity = take;
        slot.quantity -= take;
        if ( slot.quantity <= 0 ) {
            slot.item_id = LAST_BLOCK_ID;
            slot.quantity = 0;
        }
        state.ui.holding_slot = true;
    } else {
        // Place one held item into an empty or matching non-full slot.
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            slot.item_id = held.item_id;
            slot.quantity = 1;
            held.quantity--;
        } else if ( slot.item_id == held.item_id && slot.quantity < item_max_stack( held.item_id ) ) {
            slot.quantity++;
            held.quantity--;
        } else {
            return;
        }
        if ( held.quantity <= 0 ) {
            held.item_id = LAST_BLOCK_ID;
            state.ui.holding_slot = false;
        }
    }
    chest_sync_slot( state, slot_index, slot );
}

// Merge a stack into the chest (shift-click from a player inventory). Returns
// the leftover count that didn't fit.
static int chest_add( RepGameState &state, ItemID item_id, int quantity ) {
    ChestContents *contents = state.world.get_or_create_chest_contents( state.ui.chest.pos );
    if ( contents == nullptr ) {
        return quantity;
    }
    int remaining = quantity;
    for ( int i = 0; i < CHEST_SLOTS && remaining > 0; i++ ) {
        InventorySlot &slot = contents->slots[ i ];
        if ( slot.item_id == item_id && slot.quantity > 0 && slot.quantity < item_max_stack( item_id ) ) {
            const int space = item_max_stack( item_id ) - slot.quantity;
            const int to_add = remaining < space ? remaining : space;
            slot.quantity += to_add;
            remaining -= to_add;
            chest_sync_slot( state, i, slot );
        }
    }
    for ( int i = 0; i < CHEST_SLOTS && remaining > 0; i++ ) {
        InventorySlot &slot = contents->slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            const int to_add = remaining < item_max_stack( item_id ) ? remaining : item_max_stack( item_id );
            slot.item_id = item_id;
            slot.quantity = to_add;
            remaining -= to_add;
            chest_sync_slot( state, i, slot );
        }
    }
    return remaining;
}

// Shift-click on a chest slot: move the whole stack to the player inventory.
static void chest_slot_to_player( RepGameState &state, int slot_index ) {
    ChestContents *contents = state.world.get_or_create_chest_contents( state.ui.chest.pos );
    if ( contents == nullptr ) {
        return;
    }
    InventorySlot slot = contents->slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    int leftover = state.survival_inventory.addItem( slot.item_id, slot.quantity );
    if ( leftover > 0 ) {
        const int hotbar_space = state.hotbar.spaceFor( slot.item_id );
        const int to_hotbar = leftover < hotbar_space ? leftover : hotbar_space;
        if ( to_hotbar > 0 ) {
            state.hotbar.addItemWithQuantity( slot.item_id, to_hotbar, false );
            leftover -= to_hotbar;
        }
    }
    if ( leftover <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    } else {
        slot.quantity = leftover;
    }
    chest_sync_slot( state, slot_index, slot );
}

// Shift-click variants of the above: `half` moves half the stack (rounded up)
// instead of the whole stack.
static void chest_move_to_player( RepGameState &state, int slot_index, bool half ) {
    if ( !half ) {
        chest_slot_to_player( state, slot_index );
        return;
    }
    ChestContents *contents = state.world.get_or_create_chest_contents( state.ui.chest.pos );
    if ( contents == nullptr ) {
        return;
    }
    InventorySlot slot = contents->slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    const int want = ( slot.quantity + 1 ) / 2;
    int moved = want - state.survival_inventory.addItem( slot.item_id, want );
    // addItem returns the leftover; moved = want - leftover.
    if ( moved <= 0 ) {
        return;
    }
    slot.quantity -= moved;
    if ( slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    chest_sync_slot( state, slot_index, slot );
}

// Shift-click on a survival-inventory slot while a chest is open: move the
// stack (or half, for right-shift-click) into the chest.
static void survival_slot_to_chest( RepGameState &state, int slot_index, bool half ) {
    InventorySlot &slot = state.survival_inventory.getSlotsMut( )[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    const int want = half ? ( slot.quantity + 1 ) / 2 : slot.quantity;
    const int moved = want - chest_add( state, slot.item_id, want );
    if ( moved <= 0 ) {
        return;
    }
    slot.quantity -= moved;
    if ( slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    state.survival_inventory.inventory_renderer.changeSlotItem( slot_index, slot );
}

static void hotbar_slot_to_chest( RepGameState &state, int slot_index, bool half ) {
    InventorySlot slot = state.hotbar.getSlotContents( slot_index );
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    const int want = half ? ( slot.quantity + 1 ) / 2 : slot.quantity;
    const int moved = want - chest_add( state, slot.item_id, want );
    if ( moved <= 0 ) {
        return;
    }
    slot.quantity -= moved;
    if ( slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    state.hotbar.setSlotContents( slot_index, slot );
    state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
}

// --- Crafting helpers -----------------------------------------------------
// The crafting grid is 2x2 in the survival inventory and 3x3 while a
// crafting table is open (either mode). Slots live in renderer order (slot
// 0 is the BOTTOM-left cell) and get flipped to top-first for the matcher.

int repgame_crafting_dim( const RepGameState &state ) {
    return state.ui.crafting.table_open ? 3 : 2;
}

bool repgame_crafting_grid_visible( const RepGameState &state ) {
    return state.input.inventory_open && !state.ui.chest.open && ( state.ui.crafting.table_open || state.game_mode == GameMode_Survival );
}

InventoryRenderer &repgame_crafting_grid( RepGameState &state ) {
    return state.ui.crafting.table_open ? state.ui.crafting.grid3 : state.ui.crafting.grid2;
}

// Repaint the output slot from the freshly re-matched recipe. Called after
// every grid mutation.
static void crafting_recompute( RepGameState &state ) {
    const int dim = repgame_crafting_dim( state );
    ItemID top[ 9 ];
    for ( int r = 0; r < dim; r++ ) {
        for ( int c = 0; c < dim; c++ ) {
            top[ r * dim + c ] = state.ui.crafting.slots[ ( dim - 1 - r ) * dim + c ].item_id;
        }
    }
    state.ui.crafting.recipe = crafting_find_recipe( top, dim );
    if ( state.ui.crafting.recipe >= 0 ) {
        state.ui.crafting.output_slot.item_id = CRAFT_RECIPES[ state.ui.crafting.recipe ].result;
        state.ui.crafting.output_slot.quantity = CRAFT_RECIPES[ state.ui.crafting.recipe ].count;
    } else {
        state.ui.crafting.output_slot.item_id = LAST_BLOCK_ID;
        state.ui.crafting.output_slot.quantity = 0;
    }
    state.ui.crafting.output.changeSlotItem( 0, state.ui.crafting.output_slot );
}

// Lay out the grid + output panels: fixed stride taken from the survival
// panel, grid bottom pinned a quarter stride above the survival items,
// and the grid + one-stride gap + 1x1 output group centered horizontally.
void repgame_crafting_layout( RepGameState &state ) {
    const int w = static_cast<int>( state.screen.width );
    const int h = static_cast<int>( state.screen.height );
    const float stride = state.survival_inventory.inventory_renderer.getCellStride( );
    const int active_dim = repgame_crafting_dim( state );
    InventoryRenderer *grids[] = { &state.ui.crafting.grid2, &state.ui.crafting.grid3 };
    for ( int i = 0; i < 2; i++ ) {
        const int dim = i + 2;
        const float off = stride * 0.1f;
        const float grid_w = stride * dim + off;
        const float out_w = stride + off;
        const float group_w = grid_w + stride + out_w;
        InventoryRenderer &grid = *grids[ i ];
        grid.options.fixed_cell_stride = stride;
        grid.options.center_x = static_cast<int>( grid_w - group_w ) / 2;
        grid.options.pin_bottom_y = state.survival_inventory.inventory_renderer.getItemsTop( ) + static_cast<int>( stride * 0.25f );
        grid.onSizeChange( w, h, state.ui.crafting.slots );
        if ( dim == active_dim ) {
            state.ui.crafting.output.options.fixed_cell_stride = stride;
            state.ui.crafting.output.options.center_x = static_cast<int>( group_w - out_w ) / 2;
            // Vertically centered on the grid (the grid panel is square).
            state.ui.crafting.output.options.pin_bottom_y = grid.getInvBottom( ) + static_cast<int>( ( grid_w - out_w ) / 2 );
            InventorySlot out_slot = state.ui.crafting.output_slot;
            state.ui.crafting.output.onSizeChange( w, h, &out_slot );
        }
    }
}

// Generic slot click semantics for the crafting grid (the chest variants
// also sync to the chunk/server; grid slots are local state).
static void slot_pickup_or_swap( InventorySlot &slot, InventorySlot &held, bool &is_holding ) {
    if ( !is_holding ) {
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return;
        }
        held = slot;
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        is_holding = true;
    } else if ( slot.item_id == held.item_id && slot.quantity > 0 && slot.quantity < item_max_stack( held.item_id ) ) {
        const int space = item_max_stack( held.item_id ) - slot.quantity;
        const int to_move = held.quantity < space ? held.quantity : space;
        slot.quantity += to_move;
        held.quantity -= to_move;
        if ( held.quantity <= 0 ) {
            held.item_id = LAST_BLOCK_ID;
            is_holding = false;
        }
    } else {
        const InventorySlot tmp = slot;
        slot = held;
        held = tmp;
        if ( held.item_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
            is_holding = false;
        }
    }
}

static void slot_right_click( InventorySlot &slot, InventorySlot &held, bool &is_holding ) {
    if ( !is_holding ) {
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return;
        }
        const int take = ( slot.quantity + 1 ) / 2;
        held.item_id = slot.item_id;
        held.quantity = take;
        slot.quantity -= take;
        if ( slot.quantity <= 0 ) {
            slot.item_id = LAST_BLOCK_ID;
            slot.quantity = 0;
        }
        is_holding = true;
    } else {
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            slot.item_id = held.item_id;
            slot.quantity = 1;
            held.quantity--;
        } else if ( slot.item_id == held.item_id && slot.quantity < item_max_stack( held.item_id ) ) {
            slot.quantity++;
            held.quantity--;
        } else {
            return;
        }
        if ( held.quantity <= 0 ) {
            held.item_id = LAST_BLOCK_ID;
            is_holding = false;
        }
    }
}

// Shift-click on a grid slot: move the stack to the player (survival, then
// hotbar); any leftover stays in the grid.
static void crafting_slot_to_player( RepGameState &state, int slot_index ) {
    InventorySlot &slot = state.ui.crafting.slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    int leftover = state.survival_inventory.addItem( slot.item_id, slot.quantity );
    if ( leftover > 0 ) {
        const int hotbar_space = state.hotbar.spaceFor( slot.item_id );
        const int to_hotbar = leftover < hotbar_space ? leftover : hotbar_space;
        if ( to_hotbar > 0 ) {
            state.hotbar.addItemWithQuantity( slot.item_id, to_hotbar, false );
            leftover -= to_hotbar;
        }
    }
    if ( leftover <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    } else {
        slot.quantity = leftover;
    }
    repgame_crafting_grid( state ).changeSlotItem( slot_index, slot );
    state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
    crafting_recompute( state );
}

// Consume one item from every non-empty cell of the active grid.
static void crafting_consume_grid( RepGameState &state ) {
    const int dim = repgame_crafting_dim( state );
    for ( int i = 0; i < dim * dim; i++ ) {
        InventorySlot &slot = state.ui.crafting.slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            continue;
        }
        slot.quantity--;
        if ( slot.quantity <= 0 ) {
            slot.item_id = LAST_BLOCK_ID;
            slot.quantity = 0;
        }
        repgame_crafting_grid( state ).changeSlotItem( i, slot );
    }
}

// Take the result onto the cursor: allowed when the hand is empty, or holds
// the same item with room for the whole result.
static void crafting_take_result( RepGameState &state ) {
    if ( state.ui.crafting.recipe < 0 ) {
        return;
    }
    const CraftRecipe &r = CRAFT_RECIPES[ state.ui.crafting.recipe ];
    InventorySlot &held = state.ui.held_slot;
    if ( state.ui.holding_slot && ( held.item_id != r.result || held.quantity + r.count > item_max_stack( r.result ) ) ) {
        return;
    }
    crafting_consume_grid( state );
    if ( state.ui.holding_slot ) {
        held.quantity += r.count;
    } else {
        held.item_id = r.result;
        held.quantity = r.count;
        state.ui.holding_slot = true;
    }
    audio_play_sfx( "pop" );
    crafting_recompute( state );
}

// Shift-click on the output: craft repeatedly while the recipe matches and
// the result fits in survival + hotbar space (survival first).
static void crafting_craft_all( RepGameState &state ) {
    bool crafted = false;
    for ( int iter = 0; iter < 64; iter++ ) {
        if ( state.ui.crafting.recipe < 0 ) {
            break;
        }
        const CraftRecipe &r = CRAFT_RECIPES[ state.ui.crafting.recipe ];
        const int space = state.survival_inventory.spaceFor( r.result ) + state.hotbar.spaceFor( r.result );
        if ( space < r.count ) {
            break;
        }
        crafting_consume_grid( state );
        const int leftover = state.survival_inventory.addItem( r.result, r.count );
        if ( leftover > 0 ) {
            state.hotbar.addItemWithQuantity( r.result, leftover, false );
        }
        crafted = true;
        // Re-match after consuming so the loop stops once the grid runs out.
        crafting_recompute( state );
    }
    if ( crafted ) {
        audio_play_sfx( "pop" );
        state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
    }
    crafting_recompute( state );
}

// Add `quantity` of `item` to the open 3x3 table grid: merge into matching
// stacks first, then fill empty cells, both in top-left-first reading order
// (renderer slot 0 is bottom-left). Returns the leftover.
static int crafting_grid_add( RepGameState &state, ItemID item, int quantity ) {
    if ( !state.ui.crafting.table_open ) {
        return quantity;
    }
    const int max_stack = item_max_stack( item );
    bool moved = false;
    for ( int r = 0; r < 3 && quantity > 0; r++ ) {
        for ( int c = 0; c < 3 && quantity > 0; c++ ) {
            const int i = ( 2 - r ) * 3 + c;
            InventorySlot &slot = state.ui.crafting.slots[ i ];
            if ( slot.item_id == item && slot.quantity < max_stack ) {
                const int add = std::min( max_stack - slot.quantity, quantity );
                slot.quantity += add;
                quantity -= add;
                state.ui.crafting.grid3.changeSlotItem( i, slot );
                moved = true;
            }
        }
    }
    for ( int r = 0; r < 3 && quantity > 0; r++ ) {
        for ( int c = 0; c < 3 && quantity > 0; c++ ) {
            const int i = ( 2 - r ) * 3 + c;
            InventorySlot &slot = state.ui.crafting.slots[ i ];
            if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
                const int add = std::min( max_stack, quantity );
                slot.item_id = item;
                slot.quantity = add;
                quantity -= add;
                state.ui.crafting.grid3.changeSlotItem( i, slot );
                moved = true;
            }
        }
    }
    if ( moved ) {
        crafting_recompute( state );
    }
    return quantity;
}

// Shift-click on a survival-inventory slot while a crafting table is open:
// move the stack (or half, for right-shift-click) into the 3x3 grid.
static void survival_slot_to_crafting( RepGameState &state, int slot_index, bool half ) {
    InventorySlot &slot = state.survival_inventory.getSlotsMut( )[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    const int want = half ? ( slot.quantity + 1 ) / 2 : slot.quantity;
    const int moved = want - crafting_grid_add( state, slot.item_id, want );
    if ( moved <= 0 ) {
        return;
    }
    slot.quantity -= moved;
    if ( slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    state.survival_inventory.inventory_renderer.changeSlotItem( slot_index, slot );
}

static void hotbar_slot_to_crafting( RepGameState &state, int slot_index, bool half ) {
    InventorySlot slot = state.hotbar.getSlotContents( slot_index );
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return;
    }
    const int want = half ? ( slot.quantity + 1 ) / 2 : slot.quantity;
    const int moved = want - crafting_grid_add( state, slot.item_id, want );
    if ( moved <= 0 ) {
        return;
    }
    slot.quantity -= moved;
    if ( slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    state.hotbar.setSlotContents( slot_index, slot );
    state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
}

// Drag crafting: while a mouse button is held over grid slots, the held
// stack is re-distributed over the accumulated targets from the drag-start
// snapshot, so the grid shows a live preview until the button is released.
static void crafting_drag_apply( RepGameState &state ) {
    for ( int i = 0; i < 9; i++ ) {
        state.ui.crafting.slots[ i ] = state.ui.crafting.drag.snapshot_slots[ i ];
    }
    state.ui.held_slot = state.ui.crafting.drag.snapshot_held;
    crafting_drag_distribute( state.ui.crafting.slots, state.ui.crafting.drag.targets, state.ui.crafting.drag.count, state.ui.held_slot, state.ui.crafting.drag.right );
    state.ui.holding_slot = state.ui.held_slot.item_id != LAST_BLOCK_ID && state.ui.held_slot.quantity > 0;
    InventoryRenderer &grid = repgame_crafting_grid( state );
    const int dim = repgame_crafting_dim( state );
    for ( int i = 0; i < dim * dim; i++ ) {
        grid.changeSlotItem( i, state.ui.crafting.slots[ i ] );
    }
    crafting_recompute( state );
}

static void crafting_drag_start( RepGameState &state, int craft_slot, bool right ) {
    state.ui.crafting.drag.active = true;
    state.ui.crafting.drag.right = right;
    for ( int i = 0; i < 9; i++ ) {
        state.ui.crafting.drag.snapshot_slots[ i ] = state.ui.crafting.slots[ i ];
    }
    state.ui.crafting.drag.snapshot_held = state.ui.held_slot;
    state.ui.crafting.drag.targets[ 0 ] = craft_slot;
    state.ui.crafting.drag.count = 1;
    crafting_drag_apply( state );
}

// Throws `count` of `item` out of the player as an item entity: spawned just
// ahead of the eye, tossed along the look direction with a little lift, and
// un-pickable briefly (2 seconds) so it leaves the pickup radius.

static void throw_item( RepGameState &state, const ItemID item, const int count ) {
    const glm::dvec3 look = glm::dvec3( state.camera.look );
    state.world.items.spawn( item, count, state.camera.pos + look * 0.3, look * 0.045 + glm::dvec3( 0.0, 0.01, 0.0 ), 2 * UPS_RATE );
}

// Returns the grid contents to the player: survival, then hotbar, then
// thrown as an item entity when there's no room. Idempotent — the every-tick
// inventory-closed branch calls it — and resets table_open.
void repgame_crafting_return_grid( RepGameState &state ) {
    // Any in-progress drag ends; the already-applied state is final.
    state.ui.crafting.drag.active = false;
    const bool was_open = state.ui.crafting.table_open;
    state.ui.crafting.table_open = false;
    bool any = false;
    for ( int i = 0; i < 9; i++ ) {
        any = any || ( state.ui.crafting.slots[ i ].item_id != LAST_BLOCK_ID && state.ui.crafting.slots[ i ].quantity > 0 );
    }
    if ( !was_open && !any ) {
        return;
    }
    for ( int i = 0; i < 9; i++ ) {
        InventorySlot &slot = state.ui.crafting.slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            continue;
        }
        int leftover = state.survival_inventory.addItem( slot.item_id, slot.quantity );
        if ( leftover > 0 ) {
            const int hotbar_space = state.hotbar.spaceFor( slot.item_id );
            const int to_hotbar = leftover < hotbar_space ? leftover : hotbar_space;
            if ( to_hotbar > 0 ) {
                state.hotbar.addItemWithQuantity( slot.item_id, to_hotbar, false );
                leftover -= to_hotbar;
            }
        }
        if ( leftover > 0 ) {
            throw_item( state, slot.item_id, leftover );
        }
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        if ( i < 4 ) {
            state.ui.crafting.grid2.changeSlotItem( i, slot );
        }
        state.ui.crafting.grid3.changeSlotItem( i, slot );
    }
    if ( was_open ) {
        // The output panel moves back to the 2x2 group.
        repgame_crafting_layout( state );
    }
    crafting_recompute( state );
    state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
}

// A click while the inventory is open is "outside the panels" when it lands
// on no visible inventory panel rect — chest (if open), the creative or
// survival inventory, or the hotbar strip.
static bool off_inventory_panels( RepGameState &state ) {
    const int x = state.input.mouse.absPosition.x;
    const int y = state.input.mouse.absPosition.y;
    if ( state.ui.chest.open && state.ui.chest.inventory_renderer.containsPoint( x, y ) ) {
        return false;
    }
    if ( repgame_crafting_grid_visible( state ) &&
         ( repgame_crafting_grid( state ).containsPoint( x, y ) || state.ui.crafting.output.containsPoint( x, y ) ) ) {
        return false;
    }
    if ( state.game_mode == GameMode_Creative ) {
        if ( state.ui.creative.inventory_renderer.containsPoint( x, y ) ) {
            return false;
        }
    } else if ( state.survival_inventory.inventory_renderer.containsPoint( x, y ) ) {
        return false;
    }
    return !state.hotbar.inventory_renderer.containsPoint( x, y );
}

// Throws the held cursor stack (whole) or one item from it as an item
// entity; the remainder stays on the cursor.
static void throw_held_cursor_stack( RepGameState &state, const bool whole ) {
    if ( !state.ui.holding_slot || state.ui.held_slot.item_id == LAST_BLOCK_ID || state.ui.held_slot.quantity <= 0 ) {
        return;
    }
    const int qty = whole ? state.ui.held_slot.quantity : 1;
    throw_item( state, state.ui.held_slot.item_id, qty );
    state.ui.held_slot.quantity -= qty;
    if ( state.ui.held_slot.quantity <= 0 ) {
        state.ui.held_slot.item_id = LAST_BLOCK_ID;
        state.ui.holding_slot = false;
    }
}

// Non-mutating twin of give_item_to_player: how many of `item` the hotbar +
// survival inventory could accept right now. Used as `room` in multiplayer
// PICKUP_REQUESTs.
int RepGame::inventory_space_for_item( RepGameState &state, ItemID item ) {
    return state.hotbar.spaceFor( item ) + state.survival_inventory.spaceFor( item );
}

void RepGame::spawn_mob_drops( RepGameState &state, const MobTypeDef &def, const glm::dvec3 &pos ) {
    const glm::dvec3 drop_pos = pos + glm::dvec3( 0.0, 0.3, 0.0 );
    for ( int i = 0; i < 2; i++ ) {
        const MobDrop &drop = def.drops[ i ];
        if ( drop.item == LAST_BLOCK_ID ) {
            continue;
        }
        const int count = drop.min + rand( ) % ( drop.max - drop.min + 1 );
        if ( count > 0 ) {
            state.world.items.spawn( drop.item, count, drop_pos, repgame_item_drop_hop( ), 10 );
        }
    }
}

int RepGame::give_item_to_player( RepGameState &state, ItemID item, int quantity ) {
    const int bar_space = state.hotbar.spaceFor( item );
    const int to_bar = quantity < bar_space ? quantity : bar_space;
    if ( to_bar > 0 && !state.hotbar.addItemWithQuantity( item, to_bar ) ) {
        // addItemWithQuantity also rejects uncollectable items (e.g.
        // transparent blocks): nothing fit at all.
        return quantity;
    }
    return state.survival_inventory.addItem( item, quantity - to_bar );
}

void RepGame::resolve_target_mob( RepGameState &state, const glm::dvec3 &ray_origin ) {
    double mob_dist = 0.0;
    state.block_selection.target_mob = state.world.mobs.resolve_target( ray_origin, state.block_selection.ray_dir, REACH_DISTANCE,
                                                                      state.block_selection.selectionInBounds, state.block_selection.pos_destroy, &mob_dist );
    state.block_selection.target_mob_dist = mob_dist;
    if ( state.block_selection.target_mob >= 0 ) {
        state.block_selection.selectionInBounds = 0;
        state.block_selection.floatSelectionInBounds = 0;
    }
}
void RepGame::process_inventory_events( ) {
    if ( globalGameState.input.toggle_game_mode ) {
        globalGameState.input.toggle_game_mode = false;
        if ( globalGameState.game_mode == GameMode_Creative ) {
            globalGameState.game_mode = GameMode_Survival;
        } else {
            globalGameState.game_mode = GameMode_Creative;
            // Creative hides the 2x2 grid; return its contents. The 3x3
            // stays visible while a table is open.
            if ( !globalGameState.ui.crafting.table_open ) {
                repgame_crafting_return_grid( globalGameState );
            }
        }
        pr_debug( "Game mode toggled to: %s", globalGameState.game_mode == GameMode_Creative ? "Creative" : "Survival" );
    }
    if ( globalGameState.input.spawn_mob ) {
        globalGameState.input.spawn_mob = false;
        // Debug spawn cycles the non-remote types one per press.
        static const MobType debug_spawn_types[] = { MOB_PIG, MOB_COW, MOB_CHICKEN, MOB_SHEEP, MOB_ZOMBIE, MOB_CREEPER };
        static int debug_spawn_idx = 0;
        const MobType type = debug_spawn_types[ debug_spawn_idx ];
        debug_spawn_idx = ( debug_spawn_idx + 1 ) % static_cast<int>( sizeof( debug_spawn_types ) / sizeof( debug_spawn_types[ 0 ] ) );
        if ( globalGameState.world.mobs.spawn_in_front( type, globalGameState.camera.pos, glm::dvec3( globalGameState.camera.look ), globalGameState.world ) ) {
            pr_debug( "Debug-spawned %s", mob_types[ type ].name );
        }
    }
    if ( globalGameState.input.drop_item ) {
        globalGameState.input.drop_item = false;
        // Q throws one of the selected item; Ctrl+Q throws the whole stack.
        const int qty = globalGameState.input.ctrl_held ? globalGameState.hotbar.getSelectedQuantity( ) : 1;
        const ItemID held = globalGameState.hotbar.getSelectedItem( );
        if ( held != LAST_BLOCK_ID && qty > 0 && globalGameState.hotbar.consumeSelected( qty ) ) {
            throw_item( globalGameState, held, qty );
            globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
        }
    }
    // A screen tap (touch platforms) is a click at absPosition while the
    // inventory is open, or a hotbar slot tap-select while it is closed.
    const bool screen_tap = globalGameState.input.screen_tap_pending;
    globalGameState.input.screen_tap_pending = false;
    if ( globalGameState.input.menu_open ) {
        // The pause menu eats every click/tap while it's up; the inventory
        // and hotbar below it stay inert.
        if ( ( globalGameState.input.mouse.buttons.left && globalGameState.input.mouse.buttons.left_pressed_in_ui && globalGameState.input.mouse.buttons.left_click_handled == false ) || screen_tap ) {
            globalGameState.input.mouse.buttons.left_click_handled = true;
            switch ( globalGameState.ui.pause.hitTest( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y ) ) {
                case PauseMenu::BUTTON_RESUME:
                    globalGameState.input.menu_open = false;
                    break;
                case PauseMenu::BUTTON_GRAPHICS:
                    // Same as the 'r' key.
                    globalGameState.input.worldDrawQuality = static_cast<WorldDrawQuality>( ( globalGameState.input.worldDrawQuality + 1 ) % WorldDrawQuality::LAST );
                    break;
                case PauseMenu::BUTTON_SHADOW:
                    // Rebuild every live light texture under the new upload
                    // mode; the canonical light field is untouched.
                    globalGameState.input.shadow_quality = globalGameState.input.shadow_quality == SHADOW_SMOOTH ? SHADOW_FAST : SHADOW_SMOOTH;
                    light_shadow_smooth_set( globalGameState.input.shadow_quality == SHADOW_SMOOTH );
                    globalGameState.world.chunkLoader.light_shadow_refresh_all( );
                    break;
                case PauseMenu::BUTTON_RENDER_MINUS:
                    if ( globalGameState.input.render_distance > 1 ) {
                        globalGameState.input.render_distance--;
                    }
                    break;
                case PauseMenu::BUTTON_RENDER_PLUS:
                    if ( globalGameState.input.render_distance < RENDER_RADIUS_MAX ) {
                        globalGameState.input.render_distance++;
                    }
                    break;
                case PauseMenu::BUTTON_RENDER_RESET:
                    // Same as the '0' key.
                    globalGameState.input.render_distance = RENDER_RADIUS_DEFAULT;
                    break;
                case PauseMenu::BUTTON_BRIGHT_MINUS:
                    imgui_overlay_get_imgui_debug_vars( ).min_ambient = min_ambient_step( imgui_overlay_get_imgui_debug_vars( ).min_ambient, -1 );
                    break;
                case PauseMenu::BUTTON_BRIGHT_PLUS:
                    imgui_overlay_get_imgui_debug_vars( ).min_ambient = min_ambient_step( imgui_overlay_get_imgui_debug_vars( ).min_ambient, 1 );
                    break;
                case PauseMenu::BUTTON_BRIGHT_RESET:
                    imgui_overlay_get_imgui_debug_vars( ).min_ambient = MIN_AMBIENT_DEFAULT;
                    break;
                case PauseMenu::BUTTON_DEBUG_VIEW:
                    globalGameState.input.debug_mode = !globalGameState.input.debug_mode;
                    break;
                case PauseMenu::BUTTON_GAME_MODE:
                    globalGameState.input.toggle_game_mode = true;
                    break;
                case PauseMenu::BUTTON_FLYING:
                    globalGameState.input.player_flying = !globalGameState.input.player_flying;
                    break;
                case PauseMenu::BUTTON_IMGUI:
                    globalGameState.input.toggle_imgui = true;
                    break;
                case PauseMenu::BUTTON_EXIT:
                    globalGameState.input.exitGame = true;
                    break;
                default:
                    break;
            }
        }
        globalGameState.input.mouse.buttons.left_click_handled = true;
        globalGameState.input.mouse.buttons.right_click_handled = true;
        globalGameState.input.mouse.buttons.middle_click_handled = true;
        return;
    }
    if ( globalGameState.input.inventory_open ) {
        // An in-progress grid drag: releasing the button makes the applied
        // state final; while it's held, sweep newly hovered grid slots into
        // the target list and re-apply the snapshot-based distribution.
        if ( globalGameState.ui.crafting.drag.active ) {
            const bool drag_held = globalGameState.ui.crafting.drag.right ? globalGameState.input.mouse.buttons.right : globalGameState.input.mouse.buttons.left;
            if ( !drag_held || !repgame_crafting_grid_visible( globalGameState ) ) {
                globalGameState.ui.crafting.drag.active = false;
            } else {
                const int gs = repgame_crafting_grid( globalGameState ).whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                const int cells = repgame_crafting_dim( globalGameState ) * repgame_crafting_dim( globalGameState );
                if ( gs >= 0 && gs < cells ) {
                    bool seen = false;
                    for ( int i = 0; i < globalGameState.ui.crafting.drag.count; i++ ) {
                        seen = seen || globalGameState.ui.crafting.drag.targets[ i ] == gs;
                    }
                    if ( !seen && globalGameState.ui.crafting.drag.count + 1 <= globalGameState.ui.crafting.drag.snapshot_held.quantity && crafting_drag_accepts( globalGameState.ui.crafting.drag.snapshot_slots[ gs ], globalGameState.ui.crafting.drag.snapshot_held ) ) {
                        globalGameState.ui.crafting.drag.targets[ globalGameState.ui.crafting.drag.count ] = gs;
                        globalGameState.ui.crafting.drag.count++;
                        crafting_drag_apply( globalGameState );
                    }
                }
            }
            // The other button's clicks do nothing during a drag.
            if ( globalGameState.ui.crafting.drag.right ) {
                globalGameState.input.mouse.buttons.left_click_handled = true;
            } else {
                globalGameState.input.mouse.buttons.right_click_handled = true;
            }
        }
        // Crafting panel hit tests, shared by the left/middle/right click
        // branches below.
        const bool craft_ui = repgame_crafting_grid_visible( globalGameState );
        int craft_slot = -1;
        int craft_out = -1;
        if ( craft_ui ) {
            const int mx = globalGameState.input.mouse.absPosition.x;
            const int my = globalGameState.input.mouse.absPosition.y;
            const int gs = repgame_crafting_grid( globalGameState ).whichSlotClicked( mx, my );
            if ( gs >= 0 && gs < repgame_crafting_dim( globalGameState ) * repgame_crafting_dim( globalGameState ) ) {
                craft_slot = gs;
            }
            if ( globalGameState.ui.crafting.output.whichSlotClicked( mx, my ) >= 0 ) {
                craft_out = 0;
            }
        }
        if ( ( globalGameState.input.mouse.buttons.left && globalGameState.input.mouse.buttons.left_pressed_in_ui && globalGameState.input.mouse.buttons.left_click_handled == false ) || screen_tap ) {
            globalGameState.input.mouse.buttons.left_click_handled = true;
            if ( !screen_tap && globalGameState.ui.holding_slot && off_inventory_panels( globalGameState ) ) {
                // Left-click outside every panel while holding a stack throws
                // the whole stack as an item entity.
                throw_held_cursor_stack( globalGameState, true );
            } else if ( craft_slot >= 0 ) {
                // Grid slot: shift-click moves the stack to the player;
                // otherwise pick up / merge / swap.
                if ( globalGameState.input.shift_held ) {
                    crafting_slot_to_player( globalGameState, craft_slot );
                } else if ( globalGameState.ui.holding_slot && crafting_drag_accepts( globalGameState.ui.crafting.slots[ craft_slot ], globalGameState.ui.held_slot ) ) {
                    // Holding a stack that fits: begin a left-drag split.
                    crafting_drag_start( globalGameState, craft_slot, false );
                } else {
                    slot_pickup_or_swap( globalGameState.ui.crafting.slots[ craft_slot ], globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                    repgame_crafting_grid( globalGameState ).changeSlotItem( craft_slot, globalGameState.ui.crafting.slots[ craft_slot ] );
                    crafting_recompute( globalGameState );
                }
            } else if ( craft_out >= 0 ) {
                // Output slot: take the result (shift-click crafts all).
                if ( globalGameState.input.shift_held ) {
                    crafting_craft_all( globalGameState );
                } else {
                    crafting_take_result( globalGameState );
                }
            } else if ( globalGameState.ui.chest.open ) {
                // Chest UI: chest slots win, then survival, then hotbar.
                const int chest_slot = globalGameState.ui.chest.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( chest_slot >= 0 && chest_slot < CHEST_SLOTS ) {
                    if ( globalGameState.input.shift_held ) {
                        chest_slot_to_player( globalGameState, chest_slot );
                    } else {
                        chest_pickup_or_swap( globalGameState, chest_slot );
                    }
                } else {
                    const int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( slot >= 0 ) {
                        if ( globalGameState.input.shift_held ) {
                            survival_slot_to_chest( globalGameState, slot, false );
                        } else {
                            globalGameState.survival_inventory.pickupOrSwapSlot( slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                        }
                    } else {
                        const int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            if ( globalGameState.input.shift_held ) {
                                hotbar_slot_to_chest( globalGameState, hotbar_slot, false );
                            } else {
                                globalGameState.hotbar.pickupOrSwapSlot( hotbar_slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                            }
                            globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                        }
                    }
                }
            } else if ( globalGameState.game_mode == GameMode_Creative && !craft_ui ) {
                ItemID itemId = globalGameState.ui.creative.whichItemClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                // pr_debug( "Clicked on %d %d itemId:%d", globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y, itemId );
                if ( itemId != LAST_BLOCK_ID ) {
                    add_to_hotbar( true, itemId );
                } else {
                    // Clicks on the visible hotbar select its slots too.
                    const int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( hotbar_slot >= 0 ) {
                        globalGameState.hotbar.setSelectedSlot( hotbar_slot );
                        const ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                        globalGameState.ui.overlay.set_holding_item( selectedBlock );
                    }
                }
            } else {
                // Survival mode
                bool shift_held = globalGameState.input.shift_held;
                if ( shift_held ) {
                    // Shift-click: move the stack to the other inventory, or
                    // into the table grid while a crafting table is open.
                    const bool to_crafting = globalGameState.ui.crafting.table_open;
                    int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( slot >= 0 ) {
                        if ( to_crafting ) {
                            survival_slot_to_crafting( globalGameState, slot, false );
                        } else {
                            globalGameState.survival_inventory.moveToHotbar( slot, globalGameState.hotbar, false );
                        }
                        ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                        globalGameState.ui.overlay.set_holding_item( selectedBlock );
                    } else {
                        int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            if ( to_crafting ) {
                                hotbar_slot_to_crafting( globalGameState, hotbar_slot, false );
                            } else {
                                globalGameState.hotbar.moveToSurvivalInventory( hotbar_slot, globalGameState.survival_inventory );
                            }
                            ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                            globalGameState.ui.overlay.set_holding_item( selectedBlock );
                        }
                    }
                } else {
                    // Normal click: Minecraft-style pick-up / place / swap.
                    int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( slot >= 0 ) {
                        globalGameState.survival_inventory.pickupOrSwapSlot( slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                    } else {
                        int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            globalGameState.hotbar.pickupOrSwapSlot( hotbar_slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                            ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                            globalGameState.ui.overlay.set_holding_item( selectedBlock );
                        }
                    }
                }
            }
        }
        if ( globalGameState.input.mouse.buttons.middle && globalGameState.input.mouse.buttons.middle_pressed_in_ui && globalGameState.input.mouse.buttons.middle_click_handled == false ) {
            globalGameState.input.mouse.buttons.middle_click_handled = true;
            if ( craft_slot >= 0 || craft_out >= 0 ) {
                // Middle-click on crafting panels does nothing.
            } else if ( globalGameState.ui.chest.open ) {
                // Middle-click on a chest slot moves the stack to the player.
                const int chest_slot = globalGameState.ui.chest.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( chest_slot >= 0 && chest_slot < CHEST_SLOTS ) {
                    chest_slot_to_player( globalGameState, chest_slot );
                }
            } else if ( globalGameState.game_mode == GameMode_Survival ) {
                // Survival mode: middle-clicking a survival inventory slot moves
                // the stack to the hotbar.
                int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( slot >= 0 ) {
                    globalGameState.survival_inventory.moveToHotbar( slot, globalGameState.hotbar, false );
                    ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                    globalGameState.ui.overlay.set_holding_item( selectedBlock );
                }
            }
        }
        if ( globalGameState.input.mouse.buttons.right && globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.mouse.buttons.right_click_handled == false ) {
            globalGameState.input.mouse.buttons.right_click_handled = true;
            if ( globalGameState.ui.holding_slot && off_inventory_panels( globalGameState ) ) {
                // Right-click outside every panel while holding a stack
                // throws one item of it.
                throw_held_cursor_stack( globalGameState, false );
            } else if ( craft_slot >= 0 ) {
                // Grid slot right-click: take half, or place one — or begin
                // a right-drag that places one item per slot.
                if ( globalGameState.ui.holding_slot && crafting_drag_accepts( globalGameState.ui.crafting.slots[ craft_slot ], globalGameState.ui.held_slot ) ) {
                    crafting_drag_start( globalGameState, craft_slot, true );
                } else {
                    slot_right_click( globalGameState.ui.crafting.slots[ craft_slot ], globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                    repgame_crafting_grid( globalGameState ).changeSlotItem( craft_slot, globalGameState.ui.crafting.slots[ craft_slot ] );
                    crafting_recompute( globalGameState );
                }
            } else if ( craft_out >= 0 ) {
                if ( globalGameState.input.shift_held ) {
                    crafting_craft_all( globalGameState );
                } else {
                    crafting_take_result( globalGameState );
                }
            } else if ( globalGameState.ui.chest.open ) {
                const int chest_slot = globalGameState.ui.chest.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( chest_slot >= 0 && chest_slot < CHEST_SLOTS ) {
                    if ( globalGameState.input.shift_held ) {
                        chest_move_to_player( globalGameState, chest_slot, true );
                    } else {
                        chest_right_click( globalGameState, chest_slot );
                    }
                } else {
                    const int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( slot >= 0 ) {
                        if ( globalGameState.input.shift_held ) {
                            survival_slot_to_chest( globalGameState, slot, true );
                        } else {
                            globalGameState.survival_inventory.rightClickSlot( slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                        }
                    } else {
                        const int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            if ( globalGameState.input.shift_held ) {
                                hotbar_slot_to_chest( globalGameState, hotbar_slot, true );
                            } else {
                                globalGameState.hotbar.rightClickSlot( hotbar_slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                            }
                            globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                        }
                    }
                }
            } else if ( globalGameState.game_mode == GameMode_Survival || craft_ui ) {
                int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( globalGameState.input.shift_held ) {
                    // Shift-right-click: transfer half of the stack (rounded
                    // up) between the survival inventory and the hotbar, or
                    // into the table grid while a crafting table is open.
                    const bool to_crafting = globalGameState.ui.crafting.table_open;
                    if ( slot >= 0 ) {
                        if ( to_crafting ) {
                            survival_slot_to_crafting( globalGameState, slot, true );
                        } else {
                            globalGameState.survival_inventory.moveHalfToHotbar( slot, globalGameState.hotbar );
                        }
                    } else {
                        int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            if ( to_crafting ) {
                                hotbar_slot_to_crafting( globalGameState, hotbar_slot, true );
                            } else {
                                globalGameState.hotbar.moveHalfToSurvivalInventory( hotbar_slot, globalGameState.survival_inventory );
                            }
                        }
                    }
                    ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                    globalGameState.ui.overlay.set_holding_item( selectedBlock );
                } else if ( slot >= 0 ) {
                    // Right-click picks up half a stack (rounded up), or
                    // places a single held block into the slot.
                    globalGameState.survival_inventory.rightClickSlot( slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                } else {
                    int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( hotbar_slot >= 0 ) {
                        globalGameState.hotbar.rightClickSlot( hotbar_slot, globalGameState.ui.held_slot, globalGameState.ui.holding_slot );
                        ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                        globalGameState.ui.overlay.set_holding_item( selectedBlock );
                    }
                }
            }
        }
    } else {
        if ( screen_tap ) {
            // Tapping a rendered hotbar slot selects it.
            const int slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
            if ( slot >= 0 ) {
                globalGameState.hotbar.setSelectedSlot( slot );
                const ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
                globalGameState.ui.overlay.set_holding_item( selectedBlock );
            }
        }
        // Steal any left/middle click events so they don't trigger right when we open the inventory.
        globalGameState.input.mouse.buttons.left_click_handled = true;
        globalGameState.input.mouse.buttons.right_click_handled = true;
        globalGameState.input.mouse.buttons.middle_click_handled = true;
        // Closing the inventory (E / tap) also closes any open chest and
        // returns any crafting-grid contents to the player (idempotent —
        // this branch runs every tick while the inventory is closed).
        this->close_chest( );
        repgame_crafting_return_grid( globalGameState );
        // If we were holding an inventory item when the inventory closed, return
        // it to the survival inventory to avoid item loss.
        if ( globalGameState.ui.holding_slot ) {
            int leftover = globalGameState.survival_inventory.addItem( globalGameState.ui.held_slot.item_id, globalGameState.ui.held_slot.quantity );
            if ( leftover > 0 ) {
                globalGameState.hotbar.addItemWithQuantity( globalGameState.ui.held_slot.item_id, leftover );
            }
            globalGameState.ui.held_slot.item_id = LAST_BLOCK_ID;
            globalGameState.ui.held_slot.quantity = 0;
            globalGameState.ui.holding_slot = false;
        }
    }
}

// VR UI quad placement tunables: REPGAME_VR_UI_WIDTH (meters wide, default
// 1.0; height keeps the screen aspect — the layer texture is eye-sized) and
