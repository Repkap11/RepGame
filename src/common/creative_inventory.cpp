#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"
#include "common/creative_order_gen.hpp"

// Page 0 is a curated "common" section: everyday terrain/building blocks
// plus the blocks RepGame adds mechanics for (redstone sources/dust,
// buttons, pressure plates, pistons, TNT/inverse TNT, gravity blocks,
// chests, fence gates, the oak door). The generated CREATIVE_BLOCKS list starts on
// page 1, so the common entries appear there too.
static const BlockID CREATIVE_COMMON_BLOCKS[] = {
    GRASS,
    DIRT,
    STONE,
    COBBLESTONE,
    OAK_PLANKS,
    OAK_LOG,
    OAK_LEAVES,
    GLASS,
    SAND,
    GRAVEL,
    BEDROCK,
    OBSIDIAN,

    TORCH,
    REDSTONE_TORCH,
    REDSTONE_BLOCK,
    REDSTONE_WIRE_CROSS_UNPOWERED,
    STONE_BUTTON,
    WOODEN_BUTTON,
    STONE_PRESSURE_PLATE,
    WOODEN_PRESSURE_PLATE,
    REDSTONE_LAMP,
    FENCE,
    FENCE_GATE,
    WOODEN_DOOR_LOWER,
    CHEST,

    PISTON,
    STICKY_PISTON,
    TNT,
    INVERSE_TNT,
    CRAFTING_TABLE,
    FURNACE,
    ANVIL,
    LADDER,
    OAK_SLAB,
    OAK_STAIRS,
    OAK_SAPLING,
    SLIME,
};
#define CREATIVE_COMMON_COUNT ( sizeof( CREATIVE_COMMON_BLOCKS ) / sizeof( CREATIVE_COMMON_BLOCKS[ 0 ] ) )

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
    const int num_list_pages = ( CREATIVE_BLOCK_COUNT + this->num_blocks_max - 1 ) / this->num_blocks_max;
    const int num_pages = 1 + num_list_pages;
    if ( this->selected_page < 0 ) {
        this->selected_page = num_pages - 1;
    }
    if ( this->selected_page >= num_pages ) {
        this->selected_page = 0;
    }
    int i_block = ( this->selected_page - 1 ) * this->num_blocks_max;
    int i_common = 0;
    for ( int y = this->height - 1; y >= 0; y-- ) {
        for ( int x = 0; x < this->width; x++ ) {
            const int i_slot = y * this->width + x;
            InventorySlot &slot = this->slots[ i_slot ];

            if ( this->selected_page == 0 ) {
                slot.block_id = i_common < ( int )CREATIVE_COMMON_COUNT ? CREATIVE_COMMON_BLOCKS[ i_common ] : LAST_BLOCK_ID;
            } else {
                slot.block_id = i_block < ( int )CREATIVE_BLOCK_COUNT ? CREATIVE_BLOCKS[ i_block ] : LAST_BLOCK_ID;
            }
            slot.quantity = 0;
            i_block++;
            i_common++;
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
