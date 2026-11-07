#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"
#include "common/font_renderer.hpp"

void Hotbar::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = static_cast<InventorySlot *>( calloc( this->num_blocks_max, sizeof( InventorySlot ) ) );

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        InventorySlot &slot = this->slots[ i_slot ];
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        // slot.item_id = TNT;
        // slot.item_id = ( BlockID )( i_slot % 5 );
        // slot.item_id = ( BlockID )( i_slot );
    }
    setSelectedSlot( 0 );
}
void Hotbar::applySavedInventory( const InventorySlot *savedSlots ) {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        const InventorySlot &slot = savedSlots[ i_slot ];
        this->slots[ i_slot ] = slot;
        itemId_to_slot_map.emplace( slot.item_id, i_slot );
        this->inventory_renderer.changeSlotItem( i_slot, slot );
    }
}

void Hotbar::saveInventory( InventorySlot *savedSlots ) const {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        savedSlots[ i_slot ] = this->slots[ i_slot ];
    }
}

void Hotbar::onScreenSizeChange( const int width, const int height ) {
    this->inventory_renderer.onSizeChange( width, height, this->slots );
}

void Hotbar::draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader ) {
    this->inventory_renderer.draw( renderer, blocksTexture, shader );
}

void Hotbar::drawQuantities( const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp ) {
    for ( int i = 0; i < this->num_blocks_max; i++ ) {
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
        float tx = slot_x - text_width - 2.0f;
        float ty = slot_y + 2.0f;
        float tint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        font.drawString( renderer, buf, tx, ty, text_size, tint, mvp );
    }
}

int Hotbar::findOpenSlot( ) const {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        if ( this->slots[ i_slot ].item_id == LAST_BLOCK_ID )
            return i_slot;
    }
    return -1;
}
bool Hotbar::addItem( const bool alsoSelect, ItemID itemId ) {
    if ( item_is_block( itemId ) && block_definition_get_definition( static_cast<BlockID>( itemId ) )->renderOrder == RenderOrder_Transparent ) {
        // You can't keep transparent blocks in the inventory
        return false;
    }
    const auto it = this->itemId_to_slot_map.find( itemId );
    int affectedSlot;
    if ( it == this->itemId_to_slot_map.end( ) ) {
        // This is a new block
        int newSlot;
        if ( alsoSelect && this->slots[ this->selected_slot ].item_id == LAST_BLOCK_ID ) {
            newSlot = this->selected_slot;
        } else {
            newSlot = findOpenSlot( );
        }
        if ( newSlot == -1 ) {
            // No slots left, just use the selected slot.
            newSlot = this->selected_slot;
        }
        InventorySlot &slot = this->slots[ newSlot ];
        itemId_to_slot_map.erase( slot.item_id );
        slot.item_id = itemId;
        slot.quantity = 1;
        itemId_to_slot_map.emplace( itemId, newSlot );
        this->inventory_renderer.changeSlotItem( newSlot, slot );
        affectedSlot = newSlot;

    } else {
        // This is an existing block, do nothing for now.
        affectedSlot = it->second;
        // int existingSlot = it->second;
    }
    if ( alsoSelect ) {
        this->setSelectedSlot( affectedSlot );
    }
    return true;
};

bool Hotbar::addItemWithQuantity( ItemID itemId, int quantity ) {
    return addItemWithQuantity( itemId, quantity, true );
}

bool Hotbar::addItemWithQuantity( ItemID itemId, int quantity, bool prefer_selected_slot ) {
    if ( quantity <= 0 ) {
        return false;
    }
    if ( item_is_block( itemId ) && block_definition_get_definition( static_cast<BlockID>( itemId ) )->renderOrder == RenderOrder_Transparent ) {
        return false;
    }

    int remaining = quantity;

    if ( prefer_selected_slot ) {
        // First, try to add to the selected slot if it has the same item.
        {
            InventorySlot &slot = this->slots[ this->selected_slot ];
            if ( slot.item_id == itemId && slot.quantity > 0 && slot.quantity < item_max_stack( itemId ) ) {
                int space = item_max_stack( itemId ) - slot.quantity;
                int to_add = ( remaining < space ) ? remaining : space;
                slot.quantity += to_add;
                remaining -= to_add;
                this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
            }
        }
    }

    // Then, try to add to existing stacks of the same item (left to right).
    for ( int i = 0; i < this->num_blocks_max && remaining > 0; i++ ) {
        if ( prefer_selected_slot && i == this->selected_slot ) continue; // already handled above
        InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == itemId && slot.quantity > 0 && slot.quantity < item_max_stack( itemId ) ) {
            int space = item_max_stack( itemId ) - slot.quantity;
            int to_add = ( remaining < space ) ? remaining : space;
            slot.quantity += to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
        }
    }

    if ( prefer_selected_slot ) {
        // Then, try to fill the selected slot if it's empty.
        if ( remaining > 0 ) {
            InventorySlot &slot = this->slots[ this->selected_slot ];
            if ( slot.item_id == LAST_BLOCK_ID || slot.quantity == 0 ) {
                int to_add = ( remaining < item_max_stack( itemId ) ) ? remaining : item_max_stack( itemId );
                slot.item_id = itemId;
                slot.quantity = to_add;
                itemId_to_slot_map.emplace( itemId, this->selected_slot );
                remaining -= to_add;
                this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
            }
        }
    }

    // Finally, fill empty slots left to right.
    for ( int i = 0; i < this->num_blocks_max && remaining > 0; i++ ) {
        if ( prefer_selected_slot && i == this->selected_slot ) continue; // already handled above
        InventorySlot &slot = this->slots[ i ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity == 0 ) {
            int to_add = ( remaining < item_max_stack( itemId ) ) ? remaining : item_max_stack( itemId );
            slot.item_id = itemId;
            slot.quantity = to_add;
            itemId_to_slot_map.emplace( itemId, i );
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
        }
    }

    return remaining == 0;
}

bool Hotbar::canPlaceSelected( ) const {
    const InventorySlot &slot = this->slots[ this->selected_slot ];
    return item_is_block( slot.item_id ) && slot.quantity > 0;
}

bool Hotbar::consumeSelected( int amount ) {
    InventorySlot &slot = this->slots[ this->selected_slot ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity < amount ) {
        return false;
    }
    slot.quantity -= amount;
    if ( slot.quantity <= 0 ) {
        itemId_to_slot_map.erase( slot.item_id );
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
    return true;
}

int Hotbar::getSelectedQuantity( ) const {
    return this->slots[ this->selected_slot ].quantity;
}

ItemID Hotbar::incrementSelectedSlot( int offset ) {
    // Always positive modulo, so that we don't go out of bounds
    this->setSelectedSlot( ( this->selected_slot + this->num_blocks_max + offset ) % this->num_blocks_max );
    return this->slots[ this->selected_slot ].item_id;
}

ItemID Hotbar::getSelectedItem( ) const {
    return this->slots[ this->selected_slot ].item_id;
}
int Hotbar::getSelectedSlot( ) const {
    return this->selected_slot;
};

int Hotbar::findSlotWithItem( ItemID itemId, bool require_non_full ) const {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        const InventorySlot &slot = this->slots[ i_slot ];
        if ( slot.item_id == itemId && slot.quantity > 0 && ( !require_non_full || slot.quantity < item_max_stack( itemId ) ) ) {
            return i_slot;
        }
    }
    return -1;
}

int Hotbar::spaceFor( ItemID itemId ) const {
    int space = 0;
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        const InventorySlot &slot = this->slots[ i_slot ];
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            space += item_max_stack( itemId );
        } else if ( slot.item_id == itemId ) {
            space += item_max_stack( itemId ) - slot.quantity;
        }
    }
    return space;
}

void Hotbar::setSelectedSlot( const int selected_slot ) {
    this->selected_slot = selected_slot;
    this->inventory_renderer.setSelectedSlot( this->selected_slot );
}
ItemID Hotbar::dropSelectedItem( ) {
    InventorySlot &slot = this->slots[ this->selected_slot ];
    ItemID droppedItem = slot.item_id;
    if ( droppedItem == LAST_BLOCK_ID ) {
        // There is no block, so we can't drop anything
        return LAST_BLOCK_ID;
    }
    itemId_to_slot_map.erase( droppedItem );
    slot.item_id = LAST_BLOCK_ID;
    this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
    return droppedItem;
}

void Hotbar::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}

void Hotbar::pickupOrSwapSlot( int slot_index, InventorySlot &held, bool &is_holding ) {
    if ( slot_index < 0 || slot_index >= this->num_blocks_max ) {
        return;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( !is_holding ) {
        // Pick up the slot's contents.
        if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return;
        }
        held = slot;
        this->itemId_to_slot_map.erase( slot.item_id );
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
        // Place / swap. Remove old map entries for both the held item and
        // the target slot's item (if any), then re-insert after the swap.
        if ( slot.item_id != LAST_BLOCK_ID && slot.quantity > 0 ) {
            this->itemId_to_slot_map.erase( slot.item_id );
        }
        InventorySlot tmp = slot;
        slot = held;
        held = tmp;
        if ( slot.item_id != LAST_BLOCK_ID && slot.quantity > 0 ) {
            this->itemId_to_slot_map[ slot.item_id ] = slot_index;
        }
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        if ( held.item_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
            is_holding = false;
        }
    }
}

void Hotbar::rightClickSlot( int slot_index, InventorySlot &held, bool &is_holding ) {
    if ( slot_index < 0 || slot_index >= this->num_blocks_max ) {
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
            this->itemId_to_slot_map.erase( slot.item_id );
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
            const ItemID held_id = held.item_id;
            this->itemId_to_slot_map.emplace( held_id, slot_index );
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

bool Hotbar::moveToSurvivalInventory( int slot_index, class SurvivalInventory &survival_inventory ) {
    if ( slot_index < 0 || slot_index >= this->num_blocks_max ) {
        return false;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return false;
    }
    int leftover = survival_inventory.addItem( slot.item_id, slot.quantity );
    if ( leftover > 0 ) {
        // Survival inventory is full; only remove what was actually moved.
        int moved = slot.quantity - leftover;
        if ( moved <= 0 ) {
            return false;
        }
        slot.quantity = leftover;
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        return true;
    }
    // Everything was moved; clear the hotbar slot.
    this->itemId_to_slot_map.erase( slot.item_id );
    slot.item_id = LAST_BLOCK_ID;
    slot.quantity = 0;
    this->inventory_renderer.changeSlotItem( slot_index, slot );
    return true;
}

bool Hotbar::moveHalfToSurvivalInventory( int slot_index, class SurvivalInventory &survival_inventory ) {
    if ( slot_index < 0 || slot_index >= this->num_blocks_max ) {
        return false;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( slot.item_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return false;
    }
    int to_move = ( slot.quantity + 1 ) / 2;
    int leftover = survival_inventory.addItem( slot.item_id, to_move );
    int moved = to_move - leftover;
    if ( moved <= 0 ) {
        return false;
    }
    slot.quantity -= moved;
    if ( slot.quantity <= 0 ) {
        this->itemId_to_slot_map.erase( slot.item_id );
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    this->inventory_renderer.changeSlotItem( slot_index, slot );
    return true;
}

InventorySlot Hotbar::getSlotContents( int slot_index ) const {
    if ( slot_index < 0 || slot_index >= this->num_blocks_max ) {
        return { LAST_BLOCK_ID, 0 };
    }
    return this->slots[ slot_index ];
}

void Hotbar::setSlotContents( int slot_index, const InventorySlot &value ) {
    if ( slot_index < 0 || slot_index >= this->num_blocks_max ) {
        return;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( slot.item_id != LAST_BLOCK_ID && slot.item_id != value.item_id ) {
        this->itemId_to_slot_map.erase( slot.item_id );
    }
    slot = value;
    if ( value.item_id != LAST_BLOCK_ID && value.quantity > 0 ) {
        this->itemId_to_slot_map[ value.item_id ] = slot_index;
    }
    this->inventory_renderer.changeSlotItem( slot_index, slot );
}
