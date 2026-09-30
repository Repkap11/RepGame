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
    // Binds the per-frame water/SSR state (depth snapshot, inv_mvp, camera,
    // render origin). Called once before the compositing block so both the
    // refraction wobble in draw_texture_fog and draw_water can unproject
    // pixels into world space. Uniforms persist on the shared program.
    void set_water_frame_uniforms( const Texture &depthTexture, const glm::mat4 &inv_mvp, const glm::vec3 &camera_pos_rebased, const glm::vec3 &origin );
    // Water composite pass for HIGH/X_HIGH: draws the reflection texture
    // (planar for sea-level water, screen-space ray marched for elevated
    // fluids), weighted by fresnel, over stencil==1 pixels. Requires the plain
    // depth attachment (depthTexture) for SSR. animate enables the rippled
    // reflection fetch (X_HIGH); HIGH passes false for a still mirror.
    void draw_water( const Renderer &renderer, const Texture &reflectionTexture, const Texture &depthStencilTexture, const Texture &fogTexture, const Texture &skyColorTexture,
                     const Texture &sceneTexture, const Texture &depthTexture, float extraAlpha, float time_s, const glm::mat4 &mvp, const glm::mat4 &inv_mvp,
                     const glm::vec3 &origin, const glm::vec3 &camera_pos_rebased, bool animate );
    void destroy( );
};