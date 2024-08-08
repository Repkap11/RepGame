#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"

void Inventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = ( InventorySlot * )calloc( this->num_blocks_max, sizeof( InventorySlot ) );

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
void Inventory::applySavedInventory( const InventorySlot *savedSlots ) {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        const InventorySlot &slot = savedSlots[ i_slot ];
        this->slots[ i_slot ] = slot;
        blockId_to_slot_map.emplace( slot.block_id, i_slot );
        this->inventory_renderer.changeSlotItem( i_slot, slot );
    }
}

void Inventory::saveInventory( InventorySlot *savedSlots ) {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        savedSlots[ i_slot ] = this->slots[ i_slot ];
    }
}

void Inventory::onScreenSizeChange( int width, int height ) {
    this->inventory_renderer.onSizeChange( width, height, this->slots );
}

void Inventory::draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader ) {
    this->inventory_renderer.draw( renderer, blocksTexture, shader );
}

int Inventory::findOpenSlot( ) {
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        if ( this->slots[ i_slot ].block_id == LAST_BLOCK_ID )
            return i_slot;
    }
    return -1;
}
bool Inventory::addBlock( BlockID blockId ) {
    Block *block = block_definition_get_definition( blockId );
    RenderOrder renderOrder = block->renderOrder;
    if ( renderOrder == RenderOrder_Transparent || renderOrder == RenderOrder_Water ) {
        // You can't keep transparent blocks or liquid blocks in the inventory
        return false;
    }
    auto it = this->blockId_to_slot_map.find( blockId );
    if ( it == this->blockId_to_slot_map.end( ) ) {
        // This is a new block
        int newSlot;
        if ( this->selected_slot != -1 && this->slots[ this->selected_slot ].block_id == LAST_BLOCK_ID ) {
            newSlot = this->selected_slot;
        } else {
            newSlot = findOpenSlot( );
        }
        if ( newSlot == -1 ) {
            // No slots left, don't pick.
            return false;
        }
        blockId_to_slot_map.emplace( blockId, newSlot );
        InventorySlot &slot = this->slots[ newSlot ];
        slot.block_id = blockId;
        if ( newSlot != this->selected_slot ) {
            this->setSelectedSlot( newSlot );
        }
        this->inventory_renderer.changeSlotItem( this->selected_slot, slot );

    } else {
        // This is an existing block, make that slot active.
        int existingSlot = it->second;
        setSelectedSlot( existingSlot );
    }
    return true;
};
BlockID Inventory::incrementSelectedSlot( int offset ) {
    // Always positive modulo, so that we don't go out of bounds
    this->setSelectedSlot( ( this->selected_slot + this->num_blocks_max + offset ) % this->num_blocks_max );
    return this->slots[ this->selected_slot ].block_id;
}

BlockID Inventory::getSelectedBlock( ) {
    return this->slots[ this->selected_slot ].block_id;
}

void Inventory::setSelectedSlot( int selected_slot ) {
    this->selected_slot = selected_slot;
    this->inventory_renderer.setSelectedSlot( this->selected_slot );
    return;
}
BlockID Inventory::dropSelectedItem( ) {
    if ( this->selected_slot == -1 ) {
        return LAST_BLOCK_ID;
    }
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

void Inventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}
