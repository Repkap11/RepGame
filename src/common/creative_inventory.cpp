#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"
#include "common/creative_order_gen.hpp"

void CreativeInventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, const int width, const int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = static_cast<InventorySlot *>( calloc( this->num_blocks_max, sizeof( InventorySlot ) ) );

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    this->selected_page = 0;
    this->load_blocks_for_selected_page( );
}
void CreativeInventory::incrementSelectedPage( int offset ) {
    this->selected_page += offset;
    this->load_blocks_for_selected_page( );
}

BlockID CreativeInventory::whichBlockClicked( const int screen_x, const int screen_y ) {
    const int slot = this->inventory_renderer.whichSlotClicked( screen_x, screen_y );
    if ( slot < 0 || slot >= this->num_blocks_max ) {
        return LAST_BLOCK_ID;
    }
    return this->slots[ slot ].block_id;
}

void CreativeInventory::load_blocks_for_selected_page( ) {
    const int num_pages = ( CREATIVE_BLOCK_COUNT + this->num_blocks_max - 1 ) / this->num_blocks_max;
    if ( this->selected_page < 0 ) {
        this->selected_page = num_pages - 1;
    }
    if ( this->selected_page >= num_pages ) {
        this->selected_page = 0;
    }
    int i_block = this->selected_page * this->num_blocks_max;
    for ( int y = this->height - 1; y >= 0; y-- ) {
        for ( int x = 0; x < this->width; x++ ) {
            const int i_slot = y * this->width + x;
            InventorySlot &slot = this->slots[ i_slot ];

            slot.block_id = i_block < ( int )CREATIVE_BLOCK_COUNT ? CREATIVE_BLOCKS[ i_block ] : LAST_BLOCK_ID;
            slot.quantity = 0;
            i_block++;
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
