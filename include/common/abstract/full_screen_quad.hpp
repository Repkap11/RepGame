#ifndef HEADER_FULL_SCREEN_QUAD_H
#define HEADER_FULL_SCREEN_QUAD_H

#include "common/abstract/renderer.hpp"
#include "common/RenderLink.hpp"

typedef struct {
    float x, y;
    float u, v;
} FullScreenQuadVertex;

typedef struct {
} FullScreenQuadSingleInstance;

class FullScreenQuad {
    VertexBufferLayout vbl;
    RenderLink<FullScreenQuadVertex> render_link_fsq;
    Shader shader;

  public:
    void init( );
    void draw_texture( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, float extraAlpha, bool blur, bool ignoreStencil );
    void destroy( );
};

#endif