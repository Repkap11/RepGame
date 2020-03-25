#ifndef HEADER_UI_OVERLAY_H
#define HEADER_UI_OVERLAY_H
#include "common/inventory.hpp"
#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "RepGame.hpp"
#include "input.hpp"

typedef struct {
    VertexBufferLayout vbl;
    Shader shader;
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
    } draw_holding_block;
    Inventory inventory;
    int screen_width;
    int screen_height;
} UIOverlay;

typedef struct {
    float screen_x;
    float screen_y;
    float is_block;
    struct {
        float x, y, id;
    } texture;
    struct {
        float r, g, b, a;
    } tint;
} UIOverlayVertex;

void ui_overlay_init( UIOverlay *ui_overlay );
void ui_overlay_on_screen_size_change( UIOverlay *ui_overlay, int width, int height );
void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui, BlockID holding_block );
void ui_overlay_cleanup( UIOverlay *ui_overlay );

#endif