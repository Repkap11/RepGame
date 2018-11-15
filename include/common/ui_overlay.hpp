#ifndef HEADER_UI_OVERLAY_H
#define HEADER_UI_OVERLAY_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"

typedef struct {
    VertexBufferLayout vbl;
    IndexBuffer ib;
    VertexArray va;
    VertexBuffer vb;
    int vertex_size;
    Shader shader;
} UIOverlay;

typedef struct {
    float x;
    float y;
} UIOverlayVertex;

void ui_overlay_init( UIOverlay *ui_overlay );
void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, glm::mat4 &mvp_ui );

#endif