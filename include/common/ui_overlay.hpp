#ifndef HEADER_UI_OVERLAY_H
#define HEADER_UI_OVERLAY_H
#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "RepGame.hpp"
#include "input.hpp"

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

typedef struct {
    VertexBufferLayout vbl;
    Shader shader;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_crosshair;
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_held_block;
    BlockID heldBlockID;
    Inventory *inventory;
    Inventory *hotbar;
    int screen_width;
    int screen_height;
} UIOverlay;

void ui_overlay_init( UIOverlay *ui_overlay, Inventory *inventory, Inventory *hotbar );
void ui_overlay_on_screen_size_change( UIOverlay *ui_overlay, int width, int height );
void ui_overlay_set_holding_block( UIOverlay *ui_overlay, BlockID holding_block );
void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui );
void ui_overlay_cleanup( UIOverlay *ui_overlay );

#endif