#pragma once

#include "common/renderer/renderer.hpp"
#include "common/RenderLink.hpp"

typedef struct {
    float x, y;
    float u, v;
} FullScreenQuadVertex;

typedef struct {
} FullScreenQuadSingleInstance;

class FullScreenQuad {
    int maxSamples;
    VertexBufferLayout vbl;
    RenderLink<FullScreenQuadVertex> render_link_fsq;
    Shader shader;

  public:
    void init( );
    void draw_texture( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, float extraAlpha, bool blur, bool ignoreStencil );
    void destroy( );
};