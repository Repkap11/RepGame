#ifndef HEADER_UI_OVERLAY_H
#define HEADER_UI_OVERLAY_H

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
} UIOverlay;

typedef struct {
    float screen_x;
    float screen_y;
    float is_block;
    union {
        struct {
            float x, y, id, a;
        } block;
        struct {
            float r, g, b, a;
        } color;
    } data;
} UIOverlayVertex;

void ui_overlay_init( UIOverlay *ui_overlay );
void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui );

#endif