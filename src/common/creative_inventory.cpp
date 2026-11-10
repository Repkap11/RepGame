#include <string.h>

#include "common/RepGame.hpp"
#include "common/font_renderer.hpp"
#include "common/inventory_renderer.hpp"
#include "common/creative_order_gen.hpp"
#include "common/ui_overlay_buffers.hpp"

// Page 0 is a curated "common" section: everyday terrain/building blocks
// plus the blocks RepGame adds mechanics for (redstone sources/dust,
// buttons, pressure plates, pistons, TNT/inverse TNT, gravity blocks,
// chests, fence gates, the oak door). The generated CREATIVE_BLOCKS list starts on
// page 1, so the common entries appear there too.
static const ItemID CREATIVE_COMMON_BLOCKS[] = {
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

// Depth order for the search-box quad within the UI depth pass (the
// inventory draw clears depth first, so anything above the slot z works).
#define ORDER_Z_SEARCH_BOX 0.4f

// Case-insensitive char compare for the search box: uppercase snake_case
// names ("OAK_PLANKS") compare equal to a query where '_' matches ' ' or
// '_' — both sides are folded, so callers may pass a raw query.
static char search_name_char( const char c ) {
    if ( c == '_' ) {
        return ' ';
    }
    if ( c >= 'A' && c <= 'Z' ) {
        return c + ( 'a' - 'A' );
    }
    return c;
}

bool creative_search_matches( const char *name, const char *query ) {
    const size_t query_len = strlen( query );
    if ( query_len == 0 ) {
        return true;
    }
    for ( const char *n = name; *n != '\0'; n++ ) {
        size_t i = 0;
        while ( i < query_len && n[ i ] != '\0' && search_name_char( n[ i ] ) == search_name_char( query[ i ] ) ) {
            i++;
        }
        if ( i == query_len ) {
            return true;
        }
    }
    return false;
}

void CreativeInventory::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, const int width, const int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots = static_cast<InventorySlot *>( calloc( this->num_blocks_max, sizeof( InventorySlot ) ) );
    this->search_len = 0;
    this->search_text[ 0 ] = '\0';
    this->search_box_x = this->search_box_y = this->search_box_w = this->search_box_h = 0.0f;

    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, width, height );
    this->render_chain_search.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad_inventory, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    this->selected_page = 0;
    this->load_blocks_for_selected_page( );
}
void CreativeInventory::incrementSelectedPage( int offset ) {
    this->selected_page += offset;
    this->load_blocks_for_selected_page( );
}

ItemID CreativeInventory::whichItemClicked( const int screen_x, const int screen_y ) {
    const int slot = this->inventory_renderer.whichSlotClicked( screen_x, screen_y );
    if ( slot < 0 || slot >= this->num_blocks_max ) {
        return LAST_BLOCK_ID;
    }
    return this->slots[ slot ].item_id;
}

void CreativeInventory::load_blocks_for_selected_page( ) {
    // A non-empty query filters CREATIVE_BLOCKS by item name; pages then
    // cover the match list instead of the usual curated + full list.
    const bool searching = this->search_len > 0;
    int num_matches = 0;
    if ( searching ) {
        for ( unsigned int i = 0; i < CREATIVE_BLOCK_COUNT; i++ ) {
            const char *name = item_name( CREATIVE_BLOCKS[ i ] );
            if ( name != nullptr && creative_search_matches( name, this->search_text ) ) {
                num_matches++;
            }
        }
    }
    const int num_list_pages = ( CREATIVE_BLOCK_COUNT + this->num_blocks_max - 1 ) / this->num_blocks_max;
    int num_pages = searching ? ( num_matches + this->num_blocks_max - 1 ) / this->num_blocks_max : 1 + num_list_pages;
    if ( num_pages <= 0 ) {
        num_pages = 1;
    }
    if ( this->selected_page < 0 ) {
        this->selected_page = num_pages - 1;
    }
    if ( this->selected_page >= num_pages ) {
        this->selected_page = 0;
    }
    int i_block = ( this->selected_page - 1 ) * this->num_blocks_max;
    int i_common = 0;
    // Search fill cursor: matches before i_match are skipped (earlier
    // pages); the first num_blocks_max after them fill the grid.
    int i_creative = 0;
    int i_match = 0;
    const int match_skip = this->selected_page * this->num_blocks_max;
    for ( int y = this->height - 1; y >= 0; y-- ) {
        for ( int x = 0; x < this->width; x++ ) {
            const int i_slot = y * this->width + x;
            InventorySlot &slot = this->slots[ i_slot ];

            if ( searching ) {
                slot.item_id = LAST_BLOCK_ID;
                while ( i_creative < ( int )CREATIVE_BLOCK_COUNT ) {
                    const ItemID candidate = CREATIVE_BLOCKS[ i_creative++ ];
                    const char *name = item_name( candidate );
                    if ( name == nullptr || !creative_search_matches( name, this->search_text ) ) {
                        continue;
                    }
                    if ( i_match++ >= match_skip ) {
                        slot.item_id = candidate;
                        break;
                    }
                }
            } else if ( this->selected_page == 0 ) {
                slot.item_id = i_common < ( int )CREATIVE_COMMON_COUNT ? ( ItemID )CREATIVE_COMMON_BLOCKS[ i_common ] : ( ItemID )LAST_BLOCK_ID;
            } else {
                slot.item_id = i_block < ( int )CREATIVE_BLOCK_COUNT ? ( ItemID )CREATIVE_BLOCKS[ i_block ] : ( ItemID )LAST_BLOCK_ID;
            }
            slot.quantity = 0;
            i_block++;
            i_common++;
            this->inventory_renderer.changeSlotItem( i_slot, slot );
        }
    }
}

void CreativeInventory::onScreenSizeChange( int width, int height ) {
    this->screen_width = width;
    this->screen_height = height;
    this->inventory_renderer.onSizeChange( width, height, this->slots );

    // The search box is a bar spanning the panel's width, floating just
    // above its top edge with a gap proportional to a cell stride.
    const float stride = this->inventory_renderer.getCellStride( );
    this->search_box_h = stride * 0.55f;
    this->search_box_x = static_cast<float>( this->inventory_renderer.getInvLeft( ) );
    this->search_box_y = this->inventory_renderer.getInvTop( ) + stride * 0.15f;
    this->search_box_w = static_cast<float>( this->inventory_renderer.getInvRight( ) - this->inventory_renderer.getInvLeft( ) );
    this->render_chain_search.clear( );
    auto pair = this->render_chain_search.create_instance( );
    UIOverlayInstance &box = pair.second;
    box.screen_x = this->search_box_x;
    box.screen_y = this->search_box_y;
    box.screen_z = ORDER_Z_SEARCH_BOX;
    box.width = this->search_box_w;
    box.height = this->search_box_h;
    box.is_block = 0;
    box.is_isometric = 0;
    box.tint[ 0 ] = 0.25f;
    box.tint[ 1 ] = 0.25f;
    box.tint[ 2 ] = 0.25f;
    box.tint[ 3 ] = 1.0f;
    this->render_chain_search.invalidate( pair.first );
}

void CreativeInventory::draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader, const bool search_visible, FontRenderer &font, const glm::mat4 &mvp_ui ) {
    this->inventory_renderer.draw( renderer, blocksTexture, shader );
    if ( !search_visible ) {
        return;
    }
    this->render_chain_search.draw( renderer, shader );

    // Query text (or a dim hint when empty) with a trailing cursor bar.
    const float text_size = this->search_box_h * 0.6f;
    const float baseline = this->search_box_y + ( this->search_box_h - font.getCapHeight( text_size ) ) / 2.0f;
    const float text_x = this->search_box_x + this->search_box_h * 0.2f;
    char buf[ CREATIVE_SEARCH_MAX + 2 ];
    const float white[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if ( this->search_len > 0 ) {
        snprintf( buf, sizeof( buf ), "%s|", this->search_text );
        font.text_append( buf, text_x, baseline, text_size, white );
    } else {
        const float dim[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.55f };
        font.text_append( "Search items|", text_x, baseline, text_size, dim );
    }
}

void CreativeInventory::searchAppendChar( const char c ) {
    if ( this->search_len + 1 >= CREATIVE_SEARCH_MAX ) {
        return;
    }
    // Store normalized (lowercase, '_' as ' ') so the box shows what the
    // matcher compares against.
    char normalized = c;
    if ( normalized >= 'A' && normalized <= 'Z' ) {
        normalized += 'a' - 'A';
    } else if ( normalized == '_' ) {
        normalized = ' ';
    }
    this->search_text[ this->search_len++ ] = normalized;
    this->search_text[ this->search_len ] = '\0';
    this->selected_page = 0;
    this->load_blocks_for_selected_page( );
}

void CreativeInventory::searchBackspace( ) {
    if ( this->search_len > 0 ) {
        this->search_text[ --this->search_len ] = '\0';
    }
    this->selected_page = 0;
    this->load_blocks_for_selected_page( );
}

void CreativeInventory::searchClear( ) {
    this->search_len = 0;
    this->search_text[ 0 ] = '\0';
    this->selected_page = 0;
    this->load_blocks_for_selected_page( );
}

bool CreativeInventory::searchBoxContainsPoint( const int screen_x, const int screen_y ) const {
    const float x = screen_x - this->screen_width / 2.0f;
    const float y = -( screen_y - this->screen_height / 2.0f );
    return x >= this->search_box_x && x < this->search_box_x + this->search_box_w && y >= this->search_box_y && y < this->search_box_y + this->search_box_h;
}

void CreativeInventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
    this->render_chain_search.clear( );
    free( this->slots );
}
