#ifndef HEADER_UI_OVERLAY_H
#define HEADER_UI_OVERLAY_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "RepGame.hpp"

typedef struct {
    bool inventoryOpen;
} UIState;
typedef struct {
    UIState state;
    VertexBufferLayout vbl;
    Shader shader;

    struct {
        IndexBuffer ib;
        VertexArray va;
        VertexBuffer vb;
        int vertex_size;
    } draw_crosshair;
    struct {
        IndexBuffer ib;
        VertexArray va;
        VertexBuffer vb;
        int vertex_size;
    } draw_inventory;
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
void ui_overlay_update_state( UIOverlay *ui_overlay );
void ui_overlay_draw( UIOverlay *ui_overlay, Renderer *renderer, glm::mat4 &mvp_ui );

#endif