#include "common/RepGame.hpp"
#include "common/abstract/full_screen_quad.hpp"

typedef struct {
    float x, y;
    float u, v;
} FullScreenQuadVertex;

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

void full_screen_quad_init( FullScreenQuad *fsq ) {
    showErrors( );
    {
        vertex_buffer_layout_init( &fsq->vbl );
        vertex_buffer_layout_push_float( &fsq->vbl, 2 ); // FullScreenQuadVertex pos
        vertex_buffer_layout_push_float( &fsq->vbl, 2 ); // FullScreenQuadVertex uv
    }
    {
        index_buffer_init( &fsq->ib );
        index_buffer_set_data( &fsq->ib, ib_data, FSQ_INDEX_COUNT );
        // index_buffer_set_data( &fsq->ib, ib_data, 3 );
    }
    {
        vertex_buffer_init( &fsq->vb );
        vertex_buffer_set_data( &fsq->vb, vb_data, sizeof( FullScreenQuadVertex ) * FSQ_VERTEX_COUNT );
    }
    {
        vertex_array_init( &fsq->va );
        vertex_array_add_buffer( &fsq->va, &fsq->vb, &fsq->vbl, 0, 0 );
    }

    shader_init( &fsq->shader, &full_screen_quad_vertex, &full_screen_quad_fragment );
    showErrors( );
}

void full_screen_quad_draw_texture( FullScreenQuad *fsq, Renderer *renderer, Texture *texture, Texture *stencilTexture, float extraAlpha, bool blur, bool ignoreStencil ) {
    shader_set_uniform1i( &fsq->shader, "u_Texture", texture->slot );
    shader_set_uniform1i( &fsq->shader, "u_Stencil", stencilTexture->slot );
    shader_set_uniform1i( &fsq->shader, "u_IgnoreStencil", ignoreStencil );
    shader_set_uniform1f( &fsq->shader, "u_ExtraAlpha", extraAlpha );
    shader_set_uniform1i( &fsq->shader, "u_TextureSamples", blur ? MULTI_SAMPLE_SCALE / 4 : MULTI_SAMPLE_SCALE );
    shader_set_uniform1i( &fsq->shader, "u_Blur", blur );
    renderer_draw( renderer, &fsq->va, &fsq->ib, &fsq->shader, 1 );
}

void full_screen_quad_destroy( FullScreenQuad *fsq ) {
    vertex_buffer_layout_destroy( &fsq->vbl );
    index_buffer_destroy( &fsq->ib );
    shader_destroy( &fsq->shader );
    vertex_array_destroy( &fsq->va );
    vertex_buffer_destroy( &fsq->vb );
}