#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"

void CreativeInventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = ( InventorySlot * )calloc( this->num_blocks_max, sizeof( InventorySlot ) );

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    load_blocks_for_page( 0 );
}
void CreativeInventory::load_blocks_for_page( int index ) {
    int i_block = 0;
    for ( int y = this->height - 1; y >= 0; y-- ) {
        for ( int x = 0; x < this->width; x++ ) {
            int i_slot = y * this->width + x;
            InventorySlot &slot = this->slots[ i_slot ];
            // slot.block_id = LAST_BLOCK_ID;
            // slot.block_id = TNT;
            // slot.block_id = ( BlockID )( i_slot % 5 );
            bool is_pickable = false;
            do {
                i_block++;
                Block *block = block_definition_get_definition( ( BlockID )i_block );
                is_pickable = block->is_pickable;
            } while ( !is_pickable );
            slot.block_id = ( BlockID )( i_block );
            slot.quantity = 0;
            this->inventory_renderer.changeSlotItem( i_slot, slot );
        }
    }
}

void CreativeInventory::onScreenSizeChange( int width, int height ) {
    this->inventory_renderer.onSizeChange( width, height, this->slots );
}

void CreativeInventory::draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader ) {
    this->inventory_renderer.draw( renderer, blocksTexture, shader );
}

void CreativeInventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
    free( this->slots );
}
