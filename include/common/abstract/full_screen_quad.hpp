#ifndef HEADER_FULL_SCREEN_QUAD_H
#define HEADER_FULL_SCREEN_QUAD_H

#include "common/abstract/renderer.hpp"

typedef struct {
    VertexArray va;
    VertexBuffer vb;
    VertexBufferLayout vbl;
    IndexBuffer ib;
    Shader shader;
} FullScreenQuad;

void full_screen_quad_init( FullScreenQuad *fullScreenQuad );
void full_screen_quad_draw_texture( FullScreenQuad *fsq, Renderer *renderer, Texture *texture );
void full_screen_quad_destroy( FullScreenQuad *fullScreenQuad );
#endif