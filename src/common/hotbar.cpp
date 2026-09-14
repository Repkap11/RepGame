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
        slot.block_id = LAST_BLOCK_ID;
        slot.quantity = 0;
        // slot.block_id = TNT;
        // slot.block_id = ( BlockID )( i_slot % 5 );
        // slot.block_id = ( BlockID )( i_slot );
    }
    setSelectedSlot( 0 );
}
void Hotbar::applySavedInventory( const InventorySlot *savedSlots ) {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        const InventorySlot &slot = savedSlots[ i_slot ];
        this->slots[ i_slot ] = slot;
        blockId_to_slot_map.emplace( slot.block_id, i_slot );
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
        float tx = slot_x - text_width - 2.0f;
        float ty = slot_y + 2.0f;
        float tint[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
        font.drawString( renderer, buf, tx, ty, text_size, tint, mvp );
    }
}

int Hotbar::findOpenSlot( ) const {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        if ( this->slots[ i_slot ].block_id == LAST_BLOCK_ID )
            return i_slot;
    }
    return -1;
}
bool Hotbar::addBlock( const bool alsoSelect, BlockID blockId ) {
    const Block *block = block_definition_get_definition( blockId );
    const RenderOrder renderOrder = block->renderOrder;
    if ( renderOrder == RenderOrder_Transparent || renderOrder == RenderOrder_Water ) {
        // You can't keep transparent blocks or liquid blocks in the inventory
        return false;
    }
    const auto it = this->blockId_to_slot_map.find( blockId );
    int affectedSlot;
    if ( it == this->blockId_to_slot_map.end( ) ) {
        // This is a new block
        int newSlot;
        if ( alsoSelect && this->slots[ this->selected_slot ].block_id == LAST_BLOCK_ID ) {
            newSlot = this->selected_slot;
        } else {
            newSlot = findOpenSlot( );
        }
        if ( newSlot == -1 ) {
            // No slots left, just use the selected slot.
            newSlot = this->selected_slot;
        }
        InventorySlot &slot = this->slots[ newSlot ];
        blockId_to_slot_map.erase( slot.block_id );
        slot.block_id = blockId;
        slot.quantity = 1;
        blockId_to_slot_map.emplace( blockId, newSlot );
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

bool Hotbar::addBlockWithQuantity( BlockID blockId, int quantity ) {
    if ( quantity <= 0 ) {
        return false;
    }
    const Block *block = block_definition_get_definition( blockId );
    const RenderOrder renderOrder = block->renderOrder;
    if ( renderOrder == RenderOrder_Transparent || renderOrder == RenderOrder_Water ) {
        return false;
    }

    int remaining = quantity;
    int affected_slot = -1;

    // First, try to add to the selected slot if it has the same block.
    {
        InventorySlot &slot = this->slots[ this->selected_slot ];
        if ( slot.block_id == blockId && slot.quantity > 0 && slot.quantity < MAX_STACK_SIZE ) {
            int space = MAX_STACK_SIZE - slot.quantity;
            int to_add = ( remaining < space ) ? remaining : space;
            slot.quantity += to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
            affected_slot = this->selected_slot;
        }
    }

    // Then, try to add to existing stacks of the same block (left to right).
    for ( int i = 0; i < this->num_blocks_max && remaining > 0; i++ ) {
        if ( i == this->selected_slot ) continue; // already handled above
        InventorySlot &slot = this->slots[ i ];
        if ( slot.block_id == blockId && slot.quantity > 0 && slot.quantity < MAX_STACK_SIZE ) {
            int space = MAX_STACK_SIZE - slot.quantity;
            int to_add = ( remaining < space ) ? remaining : space;
            slot.quantity += to_add;
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
            if ( affected_slot < 0 ) affected_slot = i;
        }
    }

    // Then, try to fill the selected slot if it's empty.
    if ( remaining > 0 ) {
        InventorySlot &slot = this->slots[ this->selected_slot ];
        if ( slot.block_id == LAST_BLOCK_ID || slot.quantity == 0 ) {
            int to_add = ( remaining < MAX_STACK_SIZE ) ? remaining : MAX_STACK_SIZE;
            slot.block_id = blockId;
            slot.quantity = to_add;
            blockId_to_slot_map.emplace( blockId, this->selected_slot );
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
            if ( affected_slot < 0 ) affected_slot = this->selected_slot;
        }
    }

    // Finally, fill empty slots left to right.
    for ( int i = 0; i < this->num_blocks_max && remaining > 0; i++ ) {
        if ( i == this->selected_slot ) continue; // already handled above
        InventorySlot &slot = this->slots[ i ];
        if ( slot.block_id == LAST_BLOCK_ID || slot.quantity == 0 ) {
            int to_add = ( remaining < MAX_STACK_SIZE ) ? remaining : MAX_STACK_SIZE;
            slot.block_id = blockId;
            slot.quantity = to_add;
            blockId_to_slot_map.emplace( blockId, i );
            remaining -= to_add;
            this->inventory_renderer.changeSlotItem( i, slot );
            if ( affected_slot < 0 ) affected_slot = i;
        }
    }

    if ( affected_slot >= 0 ) {
        this->setSelectedSlot( affected_slot );
    }

    return remaining == 0;
}

bool Hotbar::canPlaceSelected( ) const {
    const InventorySlot &slot = this->slots[ this->selected_slot ];
    return slot.block_id != LAST_BLOCK_ID && slot.quantity > 0;
}

bool Hotbar::consumeSelected( int amount ) {
    InventorySlot &slot = this->slots[ this->selected_slot ];
    if ( slot.block_id == LAST_BLOCK_ID || slot.quantity < amount ) {
        return false;
    }
    slot.quantity -= amount;
    if ( slot.quantity <= 0 ) {
        blockId_to_slot_map.erase( slot.block_id );
        slot.block_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    }
    this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
    return true;
}

int Hotbar::getSelectedQuantity( ) const {
    return this->slots[ this->selected_slot ].quantity;
}

BlockID Hotbar::incrementSelectedSlot( int offset ) {
    // Always positive modulo, so that we don't go out of bounds
    this->setSelectedSlot( ( this->selected_slot + this->num_blocks_max + offset ) % this->num_blocks_max );
    return this->slots[ this->selected_slot ].block_id;
}

BlockID Hotbar::getSelectedBlock( ) const {
    return this->slots[ this->selected_slot ].block_id;
}
int Hotbar::getSelectedSlot( ) const {
    return this->selected_slot;
};

void Hotbar::setSelectedSlot( const int selected_slot ) {
    this->selected_slot = selected_slot;
    this->inventory_renderer.setSelectedSlot( this->selected_slot );
}
BlockID Hotbar::dropSelectedItem( ) {
    InventorySlot &slot = this->slots[ this->selected_slot ];
    BlockID droppedBlock = slot.block_id;
    if ( droppedBlock == LAST_BLOCK_ID ) {
        // There is no block, so we can't drop anything
        return LAST_BLOCK_ID;
    }
    blockId_to_slot_map.erase( droppedBlock );
    slot.block_id = LAST_BLOCK_ID;
    this->inventory_renderer.changeSlotItem( this->selected_slot, slot );
    return droppedBlock;
}

void Hotbar::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}
