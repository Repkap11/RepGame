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
    void draw_texture( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, float extraAlpha, bool blur, bool ignoreStencil, int discardZeroAlpha = 0 );
    void draw_texture_fog( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, const Texture &fogTexture, const Texture &skyColorTexture, float extraAlpha, bool blur, bool ignoreStencil, int discardZeroAlpha = 0, float time_s = 0.0f, bool ripple = false );
    // Water composite pass for X_HIGH: draws the reflection texture (planar for
    // sea-level water, screen-space ray marched for elevated fluids), weighted
    // by fresnel, over stencil==1 pixels. Requires the plain depth attachment
    // (depthTexture) for SSR.
    void draw_water( const Renderer &renderer, const Texture &reflectionTexture, const Texture &depthStencilTexture, const Texture &fogTexture, const Texture &skyColorTexture,
                     const Texture &sceneTexture, const Texture &depthTexture, float extraAlpha, float time_s, const glm::mat4 &mvp, const glm::mat4 &inv_mvp,
                     float origin_y, const glm::vec3 &sun_dir );
    void destroy( );
};