#ifndef HEADER_UI_OVERLAY_H
#define HEADER_UI_OVERLAY_H
#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include "common/RenderChain.hpp"
#include "RepGame.hpp"
#include "input.hpp"

#define ORDER_Z_INV_ITEMS_BG 0.0f
#define ORDER_Z_INV_SELECTED_SLOT 0.1f
#define ORDER_Z_INV_SLOT_BG 0.2f
#define ORDER_Z_INV_BLOCKS 0.3f

typedef struct {
    float screen_x;
    float screen_y;
    struct {
        float x, y;
    } texture;
    unsigned int is_isometric;
    unsigned int face_type;
} UIOverlayVertex;

struct UIOverlayInstance {
    float screen_x;
    float screen_y;
    float screen_z;
    float width;
    float height;
    unsigned int is_block;
    unsigned int is_isometric;
    float id_isos[ 3 ];
    float tint[ 4 ];
};

#include "common/inventory.hpp"

class UIOverlay {
    Shader shader;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_crosshair;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_held_block;
    BlockID heldBlockID;
    int screen_width;
    int screen_height;

  public:
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance );
    void on_screen_size_change( int width, int height );
    void set_holding_block( BlockID holding_block );
    void draw( Inventory &inventory, Inventory &hotbar, const Renderer &renderer, const Texture &blocksTexture, Input &input, const glm::mat4 &mvp_ui );
    void cleanup( );
};

#endif