#ifndef HEADER_INVENTORY_RENDERER_H
#define HEADER_INVENTORY_RENDERER_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "RepGame.hpp"
#include "input.hpp"
#include "ui_overlay.hpp"

// #define INVENTORY_BLOCK_SIZE 30
// #define INVENTORY_BLOCK_SPACING 80

#define INVENTORY_BLOCKS_PER_ROW 20
#define INVENTORY_BLOCKS_PER_COL 5
#define INVENTORY_MAX_SIZE ( INVENTORY_BLOCKS_PER_ROW * INVENTORY_BLOCKS_PER_COL )

typedef struct {
    BlockID block_id;
} InventorySlot;

class InventoryRenderer {
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

    // Stride between cells and blocks in the inventory.
    int item_stride_size;

    // Offset and size of cells and blocks.
    int inv_cell_size;
    int inv_cell_offset;
    int inv_block_size;
    int inv_block_offset;

    entt::entity slots_entities[ INVENTORY_MAX_SIZE ];
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_inventory_background;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_inventory_icons;

    UIOverlayInstance &init_background_cell( float r, float g, float b, float a );
    UIOverlayInstance &init_background_gray_cell( float gray );
    void renderBackground( );
    void setSize( UIOverlayInstance &vertex, float left, float bottom, float width, float height );
    void fullItemRender( InventorySlot *inventory_slots );
    void singleItemRender( int slot_index, const InventorySlot &inventory_slot );

  public:
    void init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance );
    void onSizeChange( int width, int height, InventorySlot *inventory_slots );
    void reRenderSlot( int slot_index, InventorySlot &slot );
    void draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader );
    void cleanup( );
};
#endif