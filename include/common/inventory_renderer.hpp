#pragma once

#include <limits.h>

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include <entt/entity/registry.hpp>
#include "common/RenderChain.hpp"
#include "common/item.hpp" // InventorySlot
#include "ui_overlay.hpp"

// #define INVENTORY_BLOCK_SIZE 30
// #define INVENTORY_BLOCK_SPACING 80

// Disables the pin_*_y overrides below (panel placed by gravity/centering).
#define INV_PIN_Y_DISABLED INT_MIN

typedef struct {
    float max_width_percent;
    float max_height_percent;
    float active_height_percent;
    // Fraction of the UI height basis to keep clear at the bottom of the
    // screen (e.g. the hotbar strip). Only affects centered (non
    // gravity_bottom) inventories, which shift up to stay above it.
    float bottom_reserved_percent;
    bool gravity_bottom;
    // Pin the panel to the top of the screen (chest UI sits above the player
    // inventory, which keeps its normal centered/bottom-anchored layout).
    bool gravity_top;
    bool shows_selection_slot;
    // Pixel-space (centered-origin) edge anchors applied in onSizeChange when
    // not INV_PIN_DISABLED, taking precedence over gravity/centering. Set
    // these after another panel's layout so edges align: pin_bottom_y is the
    // panel's bottom edge, pin_top_y its top edge.
    int pin_bottom_y;
    int pin_top_y;
    // Pixel cell stride override (0 = auto): the items rect is exactly
    // stride*w + offset by stride*h + offset, the panel rect equals the
    // items rect, and center_x gives the panel's horizontal center.
    float fixed_cell_stride;
    int center_x;
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
    // The pure half of onSizeChange: fills the inv_*/cell/block/selected
    // geometry fields from the screen size + options, no GL. The UI layout
    // pass calls it to size elements before the renderer rebuilds GL
    // instances; onSizeChange calls it then rebuilds.
    void compute_layout( int screen_width, int screen_height );
    // Adopts a Clay-computed items-slab position (centered UI coords):
    // shifts the items rect and the panel rect by the delta from wherever
    // compute_layout put them. Call between compute_layout and rebuild.
    void set_items_pos( int items_x, int items_y );
    // The GL half of onSizeChange: rebuilds the background/icon instances
    // at the current inv_* geometry.
    void rebuild( const InventorySlot *inventory_slots );
    void onSizeChange( int width, int height, const InventorySlot *inventory_slots );
    void changeSlotItem( int slot_index, const InventorySlot &slot );
    void setSelectedSlot( int slot_index );
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    int whichSlotClicked( int screen_x, int screen_y ) const;
    // True when the raw screen point lands inside the panel's rect (the same
    // centered, y-up coords whichSlotClicked computes).
    bool containsPoint( int screen_x, int screen_y ) const {
        const int x = screen_x - this->screen_width / 2;
        const int y = -( screen_y - this->screen_height / 2 );
        return x >= this->inv_x && x < this->inv_x + this->inv_width && y >= this->inv_y && y < this->inv_y + this->inv_height;
    }
    // Same hit test but against the visible items slab — the panel rect is
    // laid out much wider (max_width_percent of the screen) than the slab it
    // frames, so "is this click inside the window" wants the tighter one.
    bool containsItemsPoint( int screen_x, int screen_y ) const {
        const int x = screen_x - this->screen_width / 2;
        const int y = -( screen_y - this->screen_height / 2 );
        return x >= this->inv_items_x && x < this->inv_items_x + this->inv_items_width && y >= this->inv_items_y && y < this->inv_items_y + this->inv_items_height;
    }
    // Panel edges in centered screen coords (set by onSizeChange) so other
    // panels can align to them via options.pin_bottom_y / pin_top_y.
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