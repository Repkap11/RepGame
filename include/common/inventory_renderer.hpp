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
    int inv_x;
    int inv_y;
    int inv_width;
    int inv_height;
    int text_size;
    int text_spacing;

    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_inventory_background;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_inventory_icons;

  public:
    void init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance );
    void onSizeChange( int width, int height );
    void render( InventorySlot *inventory_slots );
    void draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader );
    void cleanup( );
};
#endif