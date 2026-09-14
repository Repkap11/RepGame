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
    this->inventory_renderer.options.gravity_bottom = false;
    this->inventory_renderer.options.shows_selection_slot = false;

    for ( int i_slot = 0; i_slot < this->num_slots; i_slot++ ) {
        InventorySlot &slot = this->slots[ i_slot ];
        slot.block_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
}

void SurvivalInventory::onScreenSizeChange( int width, int height ) {
    this->inventory_renderer.onSizeChange( width, height, this->slots );
}

int SurvivalInventory::addBlock( BlockID blockId, int quantity ) {
    if ( quantity <= 0 ) {
        return 0;
    }
    const Block *block = block_definition_get_definition( blockId );
    const RenderOrder renderOrder = block->renderOrder;
    if ( renderOrder == RenderOrder_Transparent || renderOrder == RenderOrder_Water ) {
        // Can't collect transparent or liquid blocks.
        return quantity;
    }

    int remaining = quantity;

    // First, try to add to existing stacks of the same block.
    for ( int i = 0; i < this->num_slots && remaining > 0; i++ ) {
        InventorySlot &slot = this->slots[ i ];
        if ( slot.block_id == blockId && slot.quantity > 0 && slot.quantity < MAX_STACK_SIZE ) {
            int space = MAX_STACK_SIZE - slot.quantity;
            int to_add = ( remaining < space ) ? remaining : space;
            slot.quantity += to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
        }
    }

    // Then, try to fill empty slots.
    for ( int i = 0; i < this->num_slots && remaining > 0; i++ ) {
        InventorySlot &slot = this->slots[ i ];
        if ( slot.block_id == LAST_BLOCK_ID || slot.quantity == 0 ) {
            int to_add = ( remaining < MAX_STACK_SIZE ) ? remaining : MAX_STACK_SIZE;
            slot.block_id = blockId;
            slot.quantity = to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
        }
    }

    return remaining;
}

bool SurvivalInventory::moveToHotbar( int slot_index, class Hotbar &hotbar ) {
    if ( slot_index < 0 || slot_index >= this->num_slots ) {
        return false;
    }
    InventorySlot &slot = this->slots[ slot_index ];
    if ( slot.block_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
        return false;
    }
    bool added = hotbar.addBlockWithQuantity( slot.block_id, slot.quantity, false );
    if ( !added ) {
        return false;
    }
    // Clear the survival inventory slot.
    slot.block_id = LAST_BLOCK_ID;
    slot.quantity = 0;
    this->inventory_renderer.changeSlotItem( slot_index, slot );
    return true;
}

bool SurvivalInventory::moveBlockToHotbar( BlockID blockId, class Hotbar &hotbar ) {
    for ( int i = 0; i < this->num_slots; i++ ) {
        if ( this->slots[ i ].block_id == blockId && this->slots[ i ].quantity > 0 ) {
            return this->moveToHotbar( i, hotbar );
        }
    }
    return false;
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
        if ( slot.block_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
            return; // Nothing to pick up.
        }
        held = slot;
        slot.block_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        is_holding = true;
    } else {
        // Place / swap.
        InventorySlot tmp = slot;
        slot = held;
        held = tmp;
        this->inventory_renderer.changeSlotItem( slot_index, slot );
        // If we placed into an empty slot, we're no longer holding.
        // If we swapped, we're now holding the swapped-out stack.
        if ( held.block_id == LAST_BLOCK_ID || held.quantity <= 0 ) {
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
        if ( slot.block_id == LAST_BLOCK_ID || slot.quantity <= 0 ) {
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
        font.drawString( renderer, buf, tx, ty, text_size, tint, mvp );
    }
}

void SurvivalInventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}
