#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"

void CreativeInventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = ( InventorySlot * )calloc( this->num_blocks_max, sizeof( InventorySlot ) );

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    this->selected_page = 0;
    this->load_blocks_for_selected_page( );
}
void CreativeInventory::incrementSelectedPage( int offset ) {
    this->selected_page += offset;
    this->load_blocks_for_selected_page( );
}

BlockID CreativeInventory::whichBlockClicked( int screen_x, int screen_y ) {
    int slot = this->inventory_renderer.whichSlotClicked( screen_x, screen_y );
    if ( slot < 0 || slot > LAST_BLOCK_ID ) {
        return LAST_BLOCK_ID;
    }
    return this->slots[ slot ].block_id;
}

void CreativeInventory::load_blocks_for_selected_page( ) {
    if ( this->selected_page < 0 ) {
        this->selected_page = LAST_BLOCK_ID / this->num_blocks_max;
    }
    int starting_block = this->selected_page * this->num_blocks_max;

    if ( starting_block >= LAST_BLOCK_ID ) {
        starting_block = 0;
        this->selected_page = 0;
    }
    int i_block = starting_block;
    for ( int y = this->height - 1; y >= 0; y-- ) {
        for ( int x = 0; x < this->width; x++ ) {
            int i_slot = y * this->width + x;
            InventorySlot &slot = this->slots[ i_slot ];

            // bool is_pickable = false;
            // do {
            //     i_block++;
            //     Block *block = block_definition_get_definition( ( BlockID )i_block );
            //     is_pickable = block->is_pickable;
            // } while ( !is_pickable );

            i_block++;
            BlockID blockId = ( BlockID )i_block;
            if ( blockId > LAST_BLOCK_ID ) {
                blockId = LAST_BLOCK_ID;
            } else {
                Block *block = block_definition_get_definition( blockId );
                if ( !block->is_pickable ) {
                    blockId = LAST_BLOCK_ID;
                }
            }
            slot.block_id = blockId;
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
