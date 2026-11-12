// World-space sign text — see include/common/world_text.hpp.

#include <math.h>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/world_text.hpp"
#include "common/font_renderer.hpp"
#include "binary_blobs.hpp"

MK_SHADER( world_text_vertex );
MK_SHADER( world_text_fragment );

struct WorldTextVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

// Board front plane in def pixels (pre-yaw, 16px/block): the shared
// 16x9x2 board slab (standing_sign_boxes / the wall-sign def box).
//   standing: board y 7..15, front plane z = 9
//   wall:     board y 4..12, front plane z = 2
// Text runs +x, up is +y, and the face points +z before the cell yaw.
static constexpr float kBoardYCenterStanding = 11.5f;
static constexpr float kBoardYCenterWall = 8.5f;
static constexpr float kBoardZStanding = 9.0f;
static constexpr float kBoardZWall = 2.0f;
// How far the ink floats off the board face — enough to avoid z-fighting
// at the draw radius, small enough to look attached at grazing angles.
static constexpr float kTextLiftPx = 0.15f;
// Layout budget on the 16px-wide board: the four lines span 7px
// vertically (1.75px pitch) and the widest legal line fills 14px.
static constexpr float kTextBlockH = 7.0f;
static constexpr float kTextWidthPx = 14.0f;

void WorldTextRenderer::init( const FontRenderer &font ) {
    this->font = &font;
    this->max_quads = 1024;

    glGenVertexArrays( 1, &vao_id );
    glBindVertexArray( vao_id );
    glGenBuffers( 1, &vbo_id );
    glBindBuffer( GL_ARRAY_BUFFER, vbo_id );

    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( WorldTextVertex ), ( const void * ) 0 );
    glEnableVertexAttribArray( 1 );
    glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( WorldTextVertex ), ( const void * )( 3 * sizeof( float ) ) );
    glEnableVertexAttribArray( 2 );
    glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( WorldTextVertex ), ( const void * )( 5 * sizeof( float ) ) );

    glBufferData( GL_ARRAY_BUFFER, max_quads * 4 * sizeof( WorldTextVertex ), NULL, GL_DYNAMIC_DRAW );

    glGenBuffers( 1, &ibo_id );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo_id );
    // Verts are bl,tl,tr,br — (0,2,1)/(0,3,2) keeps the geometric normal
    // on the +z-def side so the quad is front-facing toward a reader on
    // the sign's front (and culled from behind, like the board itself).
    unsigned int *indices = static_cast<unsigned int *>( malloc( max_quads * 6 * sizeof( unsigned int ) ) );
    for ( int i = 0; i < max_quads; i++ ) {
        const unsigned int base = i * 4;
        indices[ i * 6 + 0 ] = base + 0;
        indices[ i * 6 + 1 ] = base + 2;
        indices[ i * 6 + 2 ] = base + 1;
        indices[ i * 6 + 3 ] = base + 0;
        indices[ i * 6 + 4 ] = base + 3;
        indices[ i * 6 + 5 ] = base + 2;
    }
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, max_quads * 6 * sizeof( unsigned int ), indices, GL_STATIC_DRAW );
    free( indices );
    glBindVertexArray( 0 );

    this->shader.init( &world_text_vertex, &world_text_fragment );
    this->initialized = true;
}

void WorldTextRenderer::cleanup( ) {
    if ( !this->initialized ) {
        return;
    }
    glDeleteVertexArrays( 1, &vao_id );
    glDeleteBuffers( 1, &vbo_id );
    glDeleteBuffers( 1, &ibo_id );
    shader.destroy( );
    free( this->batch );
    this->batch = nullptr;
    this->batch_capacity = 0;
    this->batch_quads = 0;
    this->initialized = false;
}

void WorldTextRenderer::begin( ) {
    this->batch_quads = 0;
}

void WorldTextRenderer::append_sign( const glm::vec3 &sign_pos, const BlockState &state, const SignText &text, const float tint[ 4 ] ) {
    if ( !this->initialized || this->font == nullptr ) {
        return;
    }
    const bool wall = block_is_wall_sign( state.id );
    const int yaw = state.rotation & 3;
    const float z_front = ( wall ? kBoardZWall : kBoardZStanding ) + kTextLiftPx;
    const float y_center = wall ? kBoardYCenterWall : kBoardYCenterStanding;
    const float pitch = kTextBlockH / SIGN_LINES;

    const float raster = this->font->raster_size( );
    // texel -> def-px scale: the widest legal line fills kTextWidthPx,
    // and the line box stays under the pitch so lines can't overlap.
    static float s_scale = -1.0f;
    if ( s_scale < 0.0f ) {
        const char *widest = "WWWWWWWWWWWWWWW"; // SIGN_LINE_MAX W's
        const float worst_w = this->font->getTextWidthSlice( widest, SIGN_LINE_MAX, raster );
        const float line_h = this->font->getAscent( raster ) - this->font->getDescent( raster );
        s_scale = fminf( kTextWidthPx / worst_w, pitch / line_h );
    }
    const float s = s_scale;

    // Grow the staging buffer for the whole sign at once (4 lines *
    // SIGN_LINE_MAX glyphs worst case).
    const int needed = this->batch_quads + SIGN_LINES * SIGN_LINE_MAX;
    if ( needed > this->batch_capacity ) {
        int capacity = this->batch_capacity > 0 ? this->batch_capacity : 256;
        while ( capacity < needed ) {
            capacity *= 2;
        }
        this->batch = static_cast<WorldTextVertex *>( realloc( this->batch, static_cast<size_t>( capacity ) * 4 * sizeof( WorldTextVertex ) ) );
        this->batch_capacity = capacity;
    }

    // Def-space point -> world (matches chunk_vertex.glsl's yaw transform:
    // rotate about the cell center, R(v) = (-v.z, v.y, v.x) per step).
    const auto map_pt = [ & ]( float dx, float dy, float dz ) -> glm::vec3 {
        glm::vec3 p( dx * ( 1.0f / 16.0f ) - 0.5f, dy * ( 1.0f / 16.0f ) - 0.5f, dz * ( 1.0f / 16.0f ) - 0.5f );
        for ( int r = 0; r < yaw; r++ ) {
            p = glm::vec3( -p.z, p.y, p.x );
        }
        return sign_pos + glm::vec3( 0.5f ) + p;
    };

    FontGlyphQuad quads[ SIGN_LINE_MAX ];
    for ( int i = 0; i < SIGN_LINES; i++ ) {
        const std::string &line = text.lines[ i ];
        if ( line.empty( ) ) {
            continue;
        }
        const int n = this->font->glyph_quads( line.c_str( ), static_cast<int>( line.size( ) ), quads, SIGN_LINE_MAX );
        // Center each line; baselines spread by pitch about the board's
        // vertical center (line 0 = top).
        const float line_w = this->font->getTextWidthSlice( line.c_str( ), static_cast<int>( line.size( ) ), raster ) * s;
        const float base_x = 8.0f - line_w * 0.5f;
        const float base_y = y_center + ( 1.5f - i ) * pitch;
        for ( int g = 0; g < n; g++ ) {
            const FontGlyphQuad &q = quads[ g ];
            const float dx0 = base_x + q.x0 * s;
            const float dx1 = base_x + q.x1 * s;
            const float dy_top = base_y + q.y_top * s;
            const float dy_bot = base_y + q.y_bot * s;
            WorldTextVertex *quad = &this->batch[ this->batch_quads * 4 ];
            const glm::vec3 p_bl = map_pt( dx0, dy_bot, z_front );
            const glm::vec3 p_tl = map_pt( dx0, dy_top, z_front );
            const glm::vec3 p_tr = map_pt( dx1, dy_top, z_front );
            const glm::vec3 p_br = map_pt( dx1, dy_bot, z_front );
            quad[ 0 ] = { p_bl.x, p_bl.y, p_bl.z, q.s0, q.t_bot, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
            quad[ 1 ] = { p_tl.x, p_tl.y, p_tl.z, q.s0, q.t_top, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
            quad[ 2 ] = { p_tr.x, p_tr.y, p_tr.z, q.s1, q.t_top, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
            quad[ 3 ] = { p_br.x, p_br.y, p_br.z, q.s1, q.t_bot, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
            this->batch_quads++;
        }
    }
}

void WorldTextRenderer::flush( const Renderer &renderer, const glm::mat4 &mvp, const glm::vec3 &camera_pos_rebased, const glm::vec3 &origin, float fog_near, float fog_far, float water_fog_near, float water_fog_far,
                               const glm::vec3 &sky_avg_color, int underwater_wash, int opaque_fog, int water_tint_type ) {
    if ( !this->initialized || this->batch_quads == 0 ) {
        return;
    }
    shader.bind( );
    shader.set_uniform_mat4f( "u_MVP", mvp );
    shader.set_uniform3f( "u_CameraPos", camera_pos_rebased.x, camera_pos_rebased.y, camera_pos_rebased.z );
    shader.set_uniform3f( "u_Origin", origin.x, origin.y, origin.z );
    shader.set_uniform3f( "u_SkyAvgColor", sky_avg_color.r, sky_avg_color.g, sky_avg_color.b );
    shader.set_uniform1f( "u_FogNear", fog_near );
    shader.set_uniform1f( "u_FogFar", fog_far );
    shader.set_uniform1f( "u_WaterFogNear", water_fog_near );
    shader.set_uniform1f( "u_WaterFogFar", water_fog_far );
    shader.set_uniform1i( "u_Underwater", underwater_wash );
    shader.set_uniform1i( "u_OpaqueFog", opaque_fog );
    shader.set_uniform1i( "u_TintUnderWater", water_tint_type );

    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, this->font->atlas_texture( ) );
    shader.set_uniform1i( "u_Texture", 0 );

    glBindVertexArray( vao_id );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo_id );
    glBindBuffer( GL_ARRAY_BUFFER, vbo_id );
    for ( int base = 0; base < this->batch_quads; base += max_quads ) {
        const int count = this->batch_quads - base < max_quads ? this->batch_quads - base : max_quads;
        glBufferSubData( GL_ARRAY_BUFFER, 0, count * 4 * sizeof( WorldTextVertex ), this->batch + base * 4 );
        glDrawElements( GL_TRIANGLES, count * 6, GL_UNSIGNED_INT, NULL );
    }
    glBindVertexArray( 0 );
    this->batch_quads = 0;
    showErrors( );
}
