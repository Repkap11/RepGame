#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"

void Inventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = ( InventorySlot * )calloc( this->num_blocks_max, sizeof( InventorySlot ) );
    this->selected_slot = -1;

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        InventorySlot *slot = &this->slots[ i_slot ];
        slot->block_id = LAST_BLOCK_ID;
        // slot->block_id = TNT;
        // slot->block_id = ( BlockID )( i_slot % 5 );
        // slot->block_id = ( BlockID )( i_slot );
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
            this->selected_slot = newSlot;
            this->inventory_renderer.setSelectedSlot( this->selected_slot );
        }
        this->inventory_renderer.changeSlotItem( this->selected_slot, slot );

    } else {
        // This is an existing block, make that slot active.
        int existingSlot = it->second;
        this->selected_slot = existingSlot;
        this->inventory_renderer.setSelectedSlot( this->selected_slot );

        // // This is an existing block, picking it again removes it from the inventory for now
        // int existingSlot = it->second;
        // blockId_to_slot_map.erase( it );
        // InventorySlot &slot = this->slots[ existingSlot ];
        // slot.block_id = LAST_BLOCK_ID;
        // this->inventory_renderer.changeSlotItem( existingSlot, slot );
    }
    return true;
};
BlockID Inventory::incrementSelectedSlot( int offset ) {
    // Always positive modulo, so that we don't go out of bounds
    this->selected_slot = ( this->selected_slot + this->num_blocks_max + offset ) % this->num_blocks_max;
    this->inventory_renderer.setSelectedSlot( this->selected_slot );
    return this->slots[ this->selected_slot ].block_id;
}

BlockID Inventory::setSelectedSlot( int selected_slot ) {
    this->selected_slot = selected_slot;
    this->inventory_renderer.setSelectedSlot( this->selected_slot );
    return this->slots[ this->selected_slot ].block_id;
}

void Inventory::handleMouseInput( Input &inputState ) {
}

void Inventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}
