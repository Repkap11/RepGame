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
        this->vbl.init( );
        this->vbl.push_float( 2 ); // FullScreenQuadVertex pos
        this->vbl.push_float( 2 ); // FullScreenQuadVertex uv
    }

    this->render_link_fsq.init( this->vbl, vb_data, FSQ_VERTEX_COUNT, ib_data, FSQ_INDEX_COUNT );
    this->shader.init( &full_screen_quad_vertex, &full_screen_quad_fragment );
    showErrors( );
}

void FullScreenQuad::draw_texture( const Renderer &renderer, const Texture &texture, const Texture &depthStencilTexture, float extraAlpha, bool blur, bool ignoreStencil ) {
    this->shader.set_uniform1i_texture( "u_Texture", texture );
    this->shader.set_uniform1i_texture( "u_Stencil", depthStencilTexture );
    this->shader.set_uniform1i( "u_IgnoreStencil", ignoreStencil );
    this->shader.set_uniform1f( "u_ExtraAlpha", extraAlpha );
    this->shader.set_uniform1i( "u_TextureSamples", blur ? MULTI_SAMPLE_SCALE_FBO / 4 : MULTI_SAMPLE_SCALE_FBO );
    this->shader.set_uniform1i( "u_Blur", blur );
    this->render_link_fsq.draw( renderer, this->shader );
}

void FullScreenQuad::destroy( ) {
}