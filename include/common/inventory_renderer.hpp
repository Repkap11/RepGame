#pragma once

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include "RepGame.hpp"
#include "input.hpp"
#include "ui_overlay.hpp"

// #define INVENTORY_BLOCK_SIZE 30
// #define INVENTORY_BLOCK_SPACING 80

typedef struct __attribute__( ( packed ) ) {
    BlockID block_id;
    int quantity;
} InventorySlot;

typedef struct {
    float max_width_percent;
    float max_height_percent;
    float active_height_percent;
    bool gravity_bottom;
    bool shows_selection_slot;
} InventoryRenderOptions;

class InventoryRenderer {
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
    void onSizeChange( int width, int height, const InventorySlot *inventory_slots );
    void changeSlotItem( int slot_index, const InventorySlot &slot );
    void setSelectedSlot( int slot_index );
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    int whichSlotClicked( int screen_x, int screen_y ) const;
    void cleanup( );
};