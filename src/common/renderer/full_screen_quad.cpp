#include "common/RepGame.hpp"
#include "common/renderer/full_screen_quad.hpp"

#define FSQ_VERTEX_COUNT 4
FullScreenQuadVertex vb_data[ FSQ_VERTEX_COUNT ] = {
    { -1, -1, 0, 0 }, //
    { -1, 1, 0, 1 },  //
    { 1, -1, 1, 0 },  //
    { 1, 1, 1, 1 }    //
};

#define FSQ_INDEX_COUNT ( 3 * 2 )
unsigned int ib_data[ FSQ_INDEX_COUNT ] = {
    1, 0, 2, //
    1, 2, 3  //
};

MK_SHADER( full_screen_quad_vertex );
MK_SHADER( full_screen_quad_fragment );

void FullScreenQuad::init( ) {
    showErrors( );
    {
        this->vbl.push_float( 2 ); // FullScreenQuadVertex pos
        this->vbl.push_float( 2 ); // FullScreenQuadVertex uv
    }
    glGetIntegerv( GL_MAX_SAMPLES, &this->maxSamples );
    this->maxSamples = ( this->maxSamples < MSAA_SAMPLES ) ? this->maxSamples : MSAA_SAMPLES;

    this->render_link_fsq.init( this->vbl, vb_data, FSQ_VERTEX_COUNT, ib_data, FSQ_INDEX_COUNT );
    this->shader.init( &full_screen_quad_vertex, &full_screen_quad_fragment );
    showErrors( );
}

void FullScreenQuad::draw_texture( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, float extraAlpha, bool blur, bool ignoreStencil, int discardZeroAlpha ) {
    this->shader.set_uniform1i_texture( "u_Texture", texture );
    this->shader.set_uniform1i_texture( "u_Stencil", depthStencilTexture );
    // Bind u_FogTexture and u_SkyColorTexture to the same multisample texture
    // as u_Texture to avoid driver validation errors from unbound samplers,
    // even though they're not sampled in the non-fog-blend path.
    this->shader.set_uniform1i_texture( "u_FogTexture", texture );
    this->shader.set_uniform1i_texture( "u_SkyColorTexture", texture );
    this->shader.set_uniform1i( "u_IgnoreStencil", ignoreStencil );
    this->shader.set_uniform1f( "u_ExtraAlpha", extraAlpha );
    this->shader.set_uniform1i( "u_TextureSamples", blur ? 1 : this->maxSamples ); // No point sampling more than our max number of samples from our textures.
    this->shader.set_uniform1i( "u_Blur", blur );
    this->shader.set_uniform1i( "u_FogBlend", 0 );
    this->shader.set_uniform1i( "u_DiscardZeroAlpha", discardZeroAlpha );
    this->shader.set_uniform1i( "u_WaterPass", 0 );
    this->shader.set_uniform1i( "u_Ripple", 0 );
    this->shader.set_uniform1f( "u_Time", 0.0f );
    this->render_link_fsq.draw( renderer, this->shader );
}

void FullScreenQuad::draw_texture_fog( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, const Texture &fogTexture, const Texture &skyColorTexture, float extraAlpha, bool blur, bool ignoreStencil, int discardZeroAlpha, float time_s, bool ripple ) {
    this->shader.set_uniform1i_texture( "u_Texture", texture );
    this->shader.set_uniform1i_texture( "u_Stencil", depthStencilTexture );
    this->shader.set_uniform1i_texture( "u_FogTexture", fogTexture );
    this->shader.set_uniform1i_texture( "u_SkyColorTexture", skyColorTexture );
    this->shader.set_uniform1i( "u_IgnoreStencil", ignoreStencil );
    this->shader.set_uniform1f( "u_ExtraAlpha", extraAlpha );
    this->shader.set_uniform1i( "u_TextureSamples", blur ? 1 : this->maxSamples );
    this->shader.set_uniform1i( "u_Blur", blur );
    this->shader.set_uniform1i( "u_FogBlend", 1 );
    this->shader.set_uniform1i( "u_DiscardZeroAlpha", discardZeroAlpha );
    this->shader.set_uniform1i( "u_WaterPass", 0 );
    this->shader.set_uniform1i( "u_Ripple", ripple );
    this->shader.set_uniform1f( "u_Time", time_s );
    this->render_link_fsq.draw( renderer, this->shader );
}

void FullScreenQuad::set_water_frame_uniforms( const Texture &depthTexture, const glm::mat4 &inv_mvp, const glm::vec3 &camera_pos_rebased, const glm::vec3 &origin ) {
    this->shader.set_uniform1i_texture( "u_DepthTexture", depthTexture );
    this->shader.set_uniform_mat4f( "u_InvMVP", inv_mvp );
    this->shader.set_uniform1f( "u_OriginY", origin.y );
    this->shader.set_uniform2f( "u_OriginXZ", origin.x, origin.z );
    this->shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
}

void FullScreenQuad::draw_water( const Renderer &renderer, const Texture &reflectionTexture, const Texture &depthStencilTexture, const Texture &fogTexture, const Texture &skyColorTexture,
                                 const Texture &sceneTexture, const Texture &depthTexture, float extraAlpha, float time_s, const glm::mat4 &mvp, const glm::mat4 &inv_mvp,
                                 const glm::vec3 &origin, const glm::vec3 &sun_dir, const glm::vec3 &camera_pos_rebased ) {
    this->shader.set_uniform1i_texture( "u_Texture", sceneTexture );
    this->shader.set_uniform1i_texture( "u_Stencil", depthStencilTexture );
    this->shader.set_uniform1i_texture( "u_FogTexture", fogTexture );
    this->shader.set_uniform1i_texture( "u_SkyColorTexture", skyColorTexture );
    this->shader.set_uniform1i_texture( "u_ReflectionTex", reflectionTexture );
    this->shader.set_uniform1i_texture( "u_DepthTexture", depthTexture );
    this->shader.set_uniform1i( "u_IgnoreStencil", 1 );
    this->shader.set_uniform1f( "u_ExtraAlpha", extraAlpha );
    this->shader.set_uniform1i( "u_TextureSamples", this->maxSamples );
    this->shader.set_uniform1i( "u_Blur", 0 );
    this->shader.set_uniform1i( "u_FogBlend", 1 );
    this->shader.set_uniform1i( "u_DiscardZeroAlpha", 0 );
    this->shader.set_uniform1i( "u_WaterPass", 1 );
    this->shader.set_uniform1i( "u_Ripple", 1 );
    this->shader.set_uniform1f( "u_Time", time_s );
    this->shader.set_uniform_mat4f( "u_MVP", mvp );
    this->shader.set_uniform_mat4f( "u_InvMVP", inv_mvp );
    this->shader.set_uniform1f( "u_OriginY", origin.y );
    this->shader.set_uniform2f( "u_OriginXZ", origin.x, origin.z );
    this->shader.set_uniform3f( "u_SunDir", sun_dir.x, sun_dir.y, sun_dir.z );
    this->shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    this->render_link_fsq.draw( renderer, this->shader );
}

void FullScreenQuad::destroy( ) {
    this->shader.destroy( );
    this->vbl.destroy( );
}