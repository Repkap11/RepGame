#pragma once

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include <entt/entity/registry.hpp>
#include "common/RenderChain.hpp"
#include "common/item.hpp" // InventorySlot
#include "ui_overlay.hpp"

typedef struct {
    float max_width_percent;
    float max_height_percent;
    float active_height_percent;
    // Fraction of the UI height basis to keep clear at the bottom of the
    // screen. Sizing input only — ui_layout_input reads it for the
    // creative panel's floor; the layout pass positions the panel.
    float bottom_reserved_percent;
    bool shows_selection_slot;
    // Pixel cell stride override (0 = auto): the items rect is exactly
    // stride*w + offset by stride*h + offset and the panel rect equals
    // the items rect.
    float fixed_cell_stride;
} InventoryRenderOptions;

class InventoryRenderer {
    // Headless test init (test_mobs): fields are filled directly since
    // init() does GL work.
    friend int test_mobs( );
    friend void test_setup_inventories( struct RepGameState &gs );
    friend int test_creative( );
    // Cell layout
    int width;
    int height;
    int num_blocks_max;
    int vb_max_size;
    int ib_max_size;

    // Total screen size
    int screen_width;
    int screen_height;

    // Inventory position and size.
    int inv_x;
    int inv_y;
    int inv_width;
    int inv_height;

    // Region of the inventory that may show icons (unless aspect ratio constrained).
    int inv_items_x;
    int inv_items_y;
    int inv_items_height;
    int inv_items_width;

    // Offset and size of cells
    int inv_cell_size;
    int inv_cell_offset;
    float inv_cell_stride;

    // Offset and size of blocks.
    int inv_block_size;
    int inv_block_offset;

    // Offset and size of selected bg.
    int inv_selected_size;
    int inv_selected_offset;

    int selected_slot_index;
    entt::entity selected_slot_bg;

    entt::entity *slots_entities;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_inventory_background;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_inventory_icons;

    UIOverlayInstance &init_background_cell( float r, float g, float b, float a );
    UIOverlayInstance &init_background_gray_cell( float gray );
    void renderBackground( );
    static void setSize( UIOverlayInstance &vertex, float left, float bottom, float width, float height, float order_z );
    void fullItemRender( const InventorySlot *inventory_slots );
    void singleItemRender( int slot_index, const InventorySlot &inventory_slot );
    void selectedSlotRender( );

  public:
    InventoryRenderOptions options;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    // Sizes the inv_*/cell/block/selected geometry fields from the screen
    // size + options, no GL, and gives the rects a fixed baseline position
    // (items slab at the origin, panel centered on it). The UI layout
    // pass calls it to size elements, then set_items_pos places the slab.
    void compute_layout( int screen_width, int screen_height );
    // Adopts a Clay-computed items-slab position (centered UI coords):
    // shifts the items rect and the panel rect by the delta from wherever
    // compute_layout put them. Call between compute_layout and rebuild.
    void set_items_pos( int items_x, int items_y );
    // Rebuilds the background/icon GL instances at the current inv_*
    // geometry.
    void rebuild( const InventorySlot *inventory_slots );
    void changeSlotItem( int slot_index, const InventorySlot &slot );
    void setSelectedSlot( int slot_index );
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    int whichSlotClicked( int screen_x, int screen_y ) const;
    // Hit test against the visible items slab — the panel rect is laid out
    // much wider (max_width_percent of the screen) than the slab it frames,
    // so "is this click inside the window" wants the tighter one.
    bool containsItemsPoint( int screen_x, int screen_y ) const {
        const int x = screen_x - this->screen_width / 2;
        const int y = -( screen_y - this->screen_height / 2 );
        return x >= this->inv_items_x && x < this->inv_items_x + this->inv_items_width && y >= this->inv_items_y && y < this->inv_items_y + this->inv_items_height;
    }
    // Panel edges in centered screen coords — sizing inputs for the UI
    // layout pass (the creative panel rect's span).
    int getInvBottom( ) const;
    int getInvTop( ) const;
    // Panel horizontal edges (centered coords) — for positioning text in the
    // gap between two panels.
    int getInvLeft( ) const {
        return this->inv_x;
    }
    int getInvRight( ) const {
        return this->inv_x + this->inv_width;
    }
    // Cell stride and items-rect edges (centered coords), for stacking
    // fixed-stride panels at another panel's scale and for framing a panel
    // inside a window backdrop.
    float getCellStride( ) const;
    int getCellOffset( ) const {
        return this->inv_cell_offset;
    }
    int getItemsTop( ) const;
    int getItemsBottom( ) const {
        return this->inv_items_y;
    }
    int getItemsLeft( ) const {
        return this->inv_items_x;
    }
    int getItemsRight( ) const {
        return this->inv_items_x + this->inv_items_width;
    }
    // Returns the screen-space position of the bottom-right corner of the given
    // slot (for rendering quantity text). Returns false if the slot index is
    // out of bounds.
    bool getSlotPosition( int slot_index, float &out_x, float &out_y ) const;
    // Returns the block icon size and cell size (for rendering dragged items
    // at the same size as in-inventory icons).
    void getBlockMetrics( int &out_block_size, int &out_cell_size ) const;
    void cleanup( );
};