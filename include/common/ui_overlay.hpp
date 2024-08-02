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
    unsigned int is_block;
    struct {
        float x, y, id;
    } texture;
    struct {
        float r, g, b, a;
    } tint;
    unsigned int face_type;
} UIOverlayVertex;

#include "common/inventory.hpp"

typedef struct {
    VertexBufferLayout vbl;
    Shader shader;
    // RenderChain<NoElement, UIOverlayVertex> render_chain_crosshair;
    struct {
        IndexBuffer ib;
        VertexArray va;
        VertexBuffer vb;
    } draw_crosshair;
    struct {
        IndexBuffer ib_isometric;
        IndexBuffer ib_square;
        VertexArray va;
        VertexBuffer vb;
        BlockID heldBlockID;
    } draw_holding_block;
    Inventory inventory;
    int screen_width;
    int screen_height;
} UIOverlay;

void ui_overlay_init( UIOverlay *ui_overlay );
void ui_overlay_on_screen_size_change( UIOverlay *ui_overlay, int width, int height );
void ui_overlay_set_holding_block( UIOverlay *ui_overlay, BlockID holding_block );
void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui );
void ui_overlay_cleanup( UIOverlay *ui_overlay );

#endif