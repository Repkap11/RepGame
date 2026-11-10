#include <math.h>

#include "common/RepGame.hpp"
#include "common/survival_inventory.hpp"
#include "common/hotbar.hpp"
#include "common/font_renderer.hpp"

void SurvivalInventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_slots = width * height;
    this->slots = static_cast<InventorySlot *>( calloc( this->num_slots, sizeof( InventorySlot ) ) );

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    // Center the survival inventory on screen, above the hotbar.
    this->inventory_renderer.options.max_width_percent = 0.6f;
    this->inventory_renderer.options.max_height_percent = 0.5f;
    this->inventory_renderer.options.active_height_percent = 0.5f;
    this->inventory_renderer.options.bottom_reserved_percent = 0.1f;
    this->inventory_renderer.options.gravity_bottom = false;
    this->inventory_renderer.options.gravity_top = false;
    this->inventory_renderer.options.pin_bottom_y = INV_PIN_Y_DISABLED;
    this->inventory_renderer.options.pin_top_y = INV_PIN_Y_DISABLED;
    this->inventory_renderer.options.shows_selection_slot = false;
    this->inventory_renderer.options.fixed_cell_stride = 0.0f;
    this->inventory_renderer.options.center_x = 0;

    for ( int i_slot = 0; i_slot < this->num_slots; i_slot++ ) {
        InventorySlot &slot = this->slots[ i_slot ];
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
}

void SurvivalInventory::apply_layout( const UiRect &slab ) {
    this->inventory_renderer.set_items_pos( lroundf( slab.left ), lroundf( slab.bottom ) );
    this->inventory_renderer.rebuild( this->slots );
}

int SurvivalInventory::addItem( ItemID itemId, int quantity ) {
    if ( quantity <= 0 ) {
        return 0;
    }
    if ( item_is_block( itemId ) && block_definition_get_definition( static_cast<BlockID>( itemId ) )->renderOrder == RenderOrder_Transparent ) {
        // Can't collect transparent blocks.
        return quantity;
    }

    int remaining = quantity;

    // First, try to add to existing stacks of the same block.
    for ( int i = 0; i < this->num_slots && remaining > 0; i++ ) {
        InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == itemId && slot.quantity > 0 && slot.quantity < item_max_stack( itemId ) ) {
            int space = item_max_stack( itemId ) - slot.quantity;
            int to_add = ( remaining < space ) ? remaining : space;
            slot.quantity += to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
        }
    }

    // Then, try to fill empty slots.
    for ( int i = 0; i < this->num_slots && remaining > 0; i++ ) {
        InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity == 0 ) {
            int to_add = ( remaining < item_max_stack( itemId ) ) ? remaining : item_max_stack( itemId );
            slot.item_id = itemId;
            slot.quantity = to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
        }
    }

    return remaining;
}

bool SurvivalInventory::consumeItem( const ItemID itemId, const int amount ) {
    for ( int i = 0; i < this->num_slots; i++ ) {
        InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == itemId && slot.quantity >= amount ) {
            slot.quantity -= amount;
            if ( slot.quantity <= 0 ) {
                slot.item_id = LAST_BLOCK_ID;
                slot.quantity = 0;
            }
            this->inventory_renderer.changeSlotItem( i, slot );
            return true;
        }
    }
    return false;
}

int SurvivalInventory::spaceFor( ItemID itemId ) const {
    int space = 0;
    for ( int i = 0; i < this->num_slots; i++ ) {
        const InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            space += item_max_stack( itemId );
        } else if ( slot.item_id == itemId ) {
            space += item_max_stack( itemId ) - slot.quantity;
        }
    }
    return space;
}

bool SurvivalInventory::moveToHotbar( int slot_index, class Hotbar &hotbar, bool prefer_selected_slot ) {
    if ( slot_index < 0 || slot_index >= this->num_slots ) {
        return false;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return false;
    }
    bool added = hotbar.addItemWithQuantity( slot.item_id, slot.quantity, prefer_selected_slot );
    if ( !added ) {
        return false;
    }
    // Clear the survival inventory slot.
    slot.item_id = LAST_BLOCK_ID;
    slot.quantity = 0;
    this->inventory_renderer.changeSlotItem( slot_index, slot );
    return true;
}

bool SurvivalInventory::moveItemToHotbar( ItemID itemId, class Hotbar &hotbar, bool prefer_selected_slot ) {
    int full_slot = -1;
    for ( int i = 0; i < this->num_slots; i++ ) {
        const InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == itemId && slot.quantity > 0 ) {
            // Prefer moving a non-full stack over a full one.
            if ( slot.quantity < item_max_stack( itemId ) ) {
                return this->moveToHotbar( i, hotbar, prefer_selected_slot );
            }
            if ( full_slot < 0 ) {
                full_slot = i;
            }
        }
    }
    if ( full_slot >= 0 ) {
        return this->moveToHotbar( full_slot, hotbar, prefer_selected_slot );
    }
    return false;
}

bool SurvivalInventory::moveHalfToHotbar( int slot_index, class Hotbar &hotbar ) {
    if ( slot_index < 0 || slot_index >= this->num_slots ) {
        return false;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return false;
    }
    int to_move = ( slot.quantity + 1 ) / 2;
    int space = hotbar.spaceFor( slot.item_id );
    if ( to_move > space ) {
        to_move = space;
    }
    if ( to_move <= 0 ) {
        return false;
    }
    bool added = hotbar.addItemWithQuantity( slot.item_id, to_move, false );
    if ( !added ) {
        return false;
    }
    slot.quantity -= to_move;
    if ( slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    this->inventory_renderer.changeSlotItem( slot_index, slot );
    return true;
}

void SurvivalInventory::swapSlot( int slot_index, InventorySlot &held ) {
    if ( slot_index < 0 || slot_index >= this->num_slots ) {
        return;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    InventorySlot tmp = slot;
    slot = held;
    held = tmp;
    this->inventory_renderer.changeSlotItem( slot_index, slot );
}

void SurvivalInventory::pickupOrSwapSlot( int slot_index, InventorySlot &held, bool &is_holding ) {
    if ( slot_index < 0 || slot_index >= this->num_slots ) {
        return;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( !is_holding ) {
        // Pick up the slot's contents.
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return; // Nothing to pick up.
        }
        held = slot;
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        is_holding = true;
    } else {
        // Same item type: merge into the slot's stack up to its max stack,
        // with any leftover staying on the cursor.
        if ( slot.item_id == held.item_id && slot.quantity > 0 && slot.quantity < item_max_stack( held.item_id ) ) {
            int space = item_max_stack( held.item_id ) - slot.quantity;
            int to_move = ( held.quantity < space ) ? held.quantity : space;
            slot.quantity += to_move;
            held.quantity -= to_move;
            this->inventory_renderer.changeSlotItem( slot_index, slot );
            if ( held.quantity <= 0 ) {
                held.item_id = LAST_BLOCK_ID;
                is_holding = false;
            }
            return;
        }
        // Place / swap.
        InventorySlot tmp = slot;
        slot = held;
        held = tmp;
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        // If we placed into an empty slot, we're no longer holding.
        // If we swapped, we're now holding the swapped-out stack.
        if ( held.item_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
            is_holding = false;
        }
    }
}

void SurvivalInventory::rightClickSlot( int slot_index, InventorySlot &held, bool &is_holding ) {
    if ( slot_index < 0 || slot_index >= this->num_slots ) {
        return;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( !is_holding ) {
        // Pick up half of the stack, rounded up.
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return;
        }
        int take = ( slot.quantity + 1 ) / 2;
        held.item_id = slot.item_id;
        held.quantity = take;
        slot.quantity -= take;
        if ( slot.quantity <= 0 ) {
            slot.item_id = LAST_BLOCK_ID;
            slot.quantity = 0;
        }
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        is_holding = true;
    } else {
        // Place a single block into the slot.
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            slot.item_id = held.item_id;
            slot.quantity = 1;
            held.quantity--;
        } else if ( slot.item_id == held.item_id && slot.quantity < item_max_stack( held.item_id ) ) {
            slot.quantity++;
            held.quantity--;
        } else {
            // Different block type or a full stack: nothing to do.
            return;
        }
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        if ( held.quantity <= 0 ) {
            held.item_id = LAST_BLOCK_ID;
            is_holding = false;
        }
    }
}

const InventorySlot *SurvivalInventory::getSlots( ) const {
    return this->slots;
}

InventorySlot *SurvivalInventory::getSlotsMut( ) {
    return this->slots;
}

void SurvivalInventory::applySavedInventory( const InventorySlot *savedSlots ) {
    for ( int i_slot = 0; i_slot < this->num_slots; i_slot++ ) {
        const InventorySlot &slot = savedSlots[ i_slot ];
        this->slots[ i_slot ] = slot;
        this->inventory_renderer.changeSlotItem( i_slot, slot );
    }
}

void SurvivalInventory::saveInventory( InventorySlot *savedSlots ) const {
    for ( int i_slot = 0; i_slot < this->num_slots; i_slot++ ) {
        savedSlots[ i_slot ] = this->slots[ i_slot ];
    }
}

int SurvivalInventory::whichSlotClicked( int screen_x, int screen_y ) const {
    return this->inventory_renderer.whichSlotClicked( screen_x, screen_y );
}

void SurvivalInventory::draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader ) {
    this->inventory_renderer.draw( renderer, blocksTexture, shader );
}

void SurvivalInventory::drawQuantities( const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp ) {
    for ( int i = 0; i < this->num_slots; i++ ) {
        const InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            continue;
        }
        float slot_x, slot_y;
        if ( !this->inventory_renderer.getSlotPosition( i, slot_x, slot_y ) ) {
            continue;
        }
        char buf[ 16 ];
        snprintf( buf, sizeof( buf ), "%d", slot.quantity );
        float text_size = 24.0f;
        float text_width = font.getTextWidth( buf, text_size );
        // Position text at the bottom-right of the slot, with a small margin.
        float tx = slot_x - text_width - 2.0f;
        float ty = slot_y + 2.0f;
        float tint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        font.text_append( buf, tx, ty, text_size, tint );
    }
}

void SurvivalInventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}
