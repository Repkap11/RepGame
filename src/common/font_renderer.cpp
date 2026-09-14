#include "common/RepGame.hpp"
#include "common/font_renderer.hpp"
#include "common/renderer/shader.hpp"
#include "common/vendor/stb_truetype.hpp"

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS )
MK_BLOB( fonts, silkscreen, ttf );
static const unsigned char *get_font_data( int *out_size ) {
    *out_size = silkscreen.length;
    return reinterpret_cast<const unsigned char *>( silkscreen.source );
}
#elif defined( REPGAME_WASM )
#include <cstdio>
static unsigned char *wasm_font_data = nullptr;
static int wasm_font_size = 0;
static const unsigned char *get_font_data( int *out_size ) {
    if ( !wasm_font_data ) {
        FILE *f = fopen( "fonts/silkscreen.ttf", "rb" );
        if ( !f ) {
            pr_debug( "Failed to open font file" );
            *out_size = 0;
            return nullptr;
        }
        fseek( f, 0, SEEK_END );
        wasm_font_size = ftell( f );
        fseek( f, 0, SEEK_SET );
        wasm_font_data = static_cast<unsigned char *>( malloc( wasm_font_size ) );
        fread( wasm_font_data, 1, wasm_font_size, f );
        fclose( f );
        pr_debug( "Loaded WASM font size:%d", wasm_font_size );
    }
    *out_size = wasm_font_size;
    return wasm_font_data;
}
#elif defined( REPGAME_ANDROID )
extern unsigned char *repgame_android_getAssetBytes( const char *filename, int *out_size );
static const unsigned char *get_font_data( int *out_size ) {
    return repgame_android_getAssetBytes( "fonts/silkscreen.ttf", out_size );
}
#endif

MK_SHADER( text_vertex );
MK_SHADER( text_fragment );

// Interleaved vertex: position (x,y) + UV (u,v) per corner.
struct TextVertex {
    float x, y;
    float u, v;
};

void FontRenderer::init( ) {
    showErrors( );

    int font_data_size = 0;
    const unsigned char *font_data = get_font_data( &font_data_size );

    if ( !font_data || font_data_size <= 0 ) {
        pr_debug( "FontRenderer: no font data available" );
        return;
    }

    stbtt_fontinfo info;
    int ok = stbtt_InitFont( &info, font_data, stbtt_GetFontOffsetForIndex( font_data, 0 ) );
    if ( !ok ) {
        pr_debug( "FontRenderer: stbtt_InitFont failed" );
        return;
    }

    this->font_size = 48.0f;
    this->first_char = 32;
    this->num_chars = 95; // 32..126

    float scale = stbtt_ScaleForPixelHeight( &info, font_size );

    int f_ascent, f_descent, f_linegap;
    stbtt_GetFontVMetrics( &info, &f_ascent, &f_descent, &f_linegap );
    this->ascent = f_ascent * scale;
    this->descent = f_descent * scale;
    this->line_gap = f_linegap * scale;

    // Use stbtt_PackBegin/stbtt_PackFontRange to create the atlas.
    // This handles glyph packing and UV coordinate computation correctly.
    this->atlas_width = 2048;
    this->atlas_height = 512;

    unsigned char *atlas_pixels = static_cast<unsigned char *>( calloc( atlas_width * atlas_height, 1 ) );

    stbtt_pack_context pack_ctx;
    int pack_ok = stbtt_PackBegin( &pack_ctx, atlas_pixels, atlas_width, atlas_height, 0, 1, nullptr );
    if ( !pack_ok ) {
        pr_debug( "FontRenderer: stbtt_PackBegin failed" );
        free( atlas_pixels );
        return;
    }

    stbtt_PackFontRange( &pack_ctx, font_data, 0, font_size, first_char, num_chars, glyphs );
    stbtt_PackEnd( &pack_ctx );

    // Convert the single-channel atlas to RGBA for maximum driver compatibility.
    unsigned char *atlas_rgba = static_cast<unsigned char *>( malloc( atlas_width * atlas_height * 4 ) );
    for ( int i = 0; i < atlas_width * atlas_height; i++ ) {
        unsigned char a = atlas_pixels[ i ];
        atlas_rgba[ i * 4 + 0 ] = 255;
        atlas_rgba[ i * 4 + 1 ] = 255;
        atlas_rgba[ i * 4 + 2 ] = 255;
        atlas_rgba[ i * 4 + 3 ] = a;
    }
    free( atlas_pixels );

    // Upload atlas as an RGBA texture.
    glGenTextures( 1, &atlas_texture_id );
    glBindTexture( GL_TEXTURE_2D, atlas_texture_id );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, atlas_width, atlas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas_rgba );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    showErrors( );

    free( atlas_rgba );

    // Set up raw GL buffers for dynamic text quads.
    this->max_quads = 1024;

    glGenVertexArrays( 1, &vao_id );
    glBindVertexArray( vao_id );

    glGenBuffers( 1, &vbo_id );
    glBindBuffer( GL_ARRAY_BUFFER, vbo_id );

    // Position attribute (location 0): 2 floats
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, sizeof( TextVertex ), ( const void * ) 0 );
    // UV attribute (location 1): 2 floats
    glEnableVertexAttribArray( 1 );
    glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( TextVertex ), ( const void * )( 2 * sizeof( float ) ) );

    // Allocate VBO storage with GL_DYNAMIC_DRAW for per-frame updates.
    glBufferData( GL_ARRAY_BUFFER, max_quads * 4 * sizeof( TextVertex ), NULL, GL_DYNAMIC_DRAW );

    // Build index buffer for max_quads quads.
    glGenBuffers( 1, &ibo_id );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo_id );
    unsigned int *indices = static_cast<unsigned int *>( malloc( max_quads * 6 * sizeof( unsigned int ) ) );
    for ( int i = 0; i < max_quads; i++ ) {
        unsigned int base = i * 4;
        indices[ i * 6 + 0 ] = base + 0;
        indices[ i * 6 + 1 ] = base + 1;
        indices[ i * 6 + 2 ] = base + 2;
        indices[ i * 6 + 3 ] = base + 0;
        indices[ i * 6 + 4 ] = base + 2;
        indices[ i * 6 + 5 ] = base + 3;
    }
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, max_quads * 6 * sizeof( unsigned int ), indices, GL_STATIC_DRAW );
    free( indices );

    glBindVertexArray( 0 );

    this->shader.init( &text_vertex, &text_fragment );
    showErrors( );

    pr_debug( "FontRenderer initialized: atlas %dx%d, ascent=%.1f descent=%.1f", atlas_width, atlas_height, ascent, descent );
}

float FontRenderer::getTextWidth( const char *text, float size ) const {
    float scale = size / font_size;
    float width = 0;
    for ( const char *p = text; *p; p++ ) {
        int codepoint = static_cast<unsigned char>( *p );
        if ( codepoint < first_char || codepoint >= first_char + num_chars ) continue;
        const stbtt_packedchar &g = glyphs[ codepoint - first_char ];
        width += g.xadvance * scale;
    }
    return width;
}

float FontRenderer::getAscent( float size ) const {
    return ascent * ( size / font_size );
}

float FontRenderer::getDescent( float size ) const {
    return descent * ( size / font_size );
}

void FontRenderer::drawString( const Renderer &renderer, const char *text, float x, float y, float size, const float tint[ 4 ], const glm::mat4 &mvp ) {
    float scale = size / font_size;
    float pen_x = 0;
    float pen_y = 0;

    // Build vertex data for all glyphs using stbtt_GetPackedQuad.
    // We render at the native font_size (pen starts at 0,0), then scale
    // and translate the final quad positions to (x, y) at the requested size.
    TextVertex *vertices = static_cast<TextVertex *>( malloc( max_quads * 4 * sizeof( TextVertex ) ) );
    int quad_count = 0;

    for ( const char *p = text; *p && quad_count < max_quads; p++ ) {
        int codepoint = static_cast<unsigned char>( *p );
        if ( codepoint < first_char || codepoint >= first_char + num_chars ) continue;

        stbtt_aligned_quad q;
        stbtt_GetPackedQuad( glyphs, atlas_width, atlas_height, codepoint - first_char, &pen_x, &pen_y, &q, 0 );

        // Scale quad positions from native font_size to requested size,
        // and translate to (x, y).
        // stbtt uses y-down coordinates: y0=top (negative, above pen),
        // y1=bottom (positive, below pen). Our screen is y-up, so flip Y.
        float sx0 = x + q.x0 * scale;
        float sy0 = y - q.y0 * scale;  // top of glyph (above baseline)
        float sx1 = x + q.x1 * scale;
        float sy1 = y - q.y1 * scale;  // bottom of glyph (below baseline)

        TextVertex *quad = &vertices[ quad_count * 4 ];
        // stbtt uses y-down: y0=top (negative, above pen), y1=bottom (positive, below pen).
        // Our screen is y-up, so flip Y: sy0 = top (higher y), sy1 = bottom (lower y).
        // stbtt texture coords: t0=top of glyph in atlas, t1=bottom.
        // glTexImage2D puts atlas row 0 at v=0 (bottom of texture in OpenGL),
        // so the atlas is flipped in the texture. Swap t0/t1 to compensate.
        // Bottom-left (lower y on screen = bottom of glyph)
        quad[ 0 ] = { sx0, sy1, q.s0, q.t1 };
        // Top-left (higher y on screen = top of glyph)
        quad[ 1 ] = { sx0, sy0, q.s0, q.t0 };
        // Top-right (higher y on screen = top of glyph)
        quad[ 2 ] = { sx1, sy0, q.s1, q.t0 };
        // Bottom-right (lower y on screen = bottom of glyph)
        quad[ 3 ] = { sx1, sy1, q.s1, q.t1 };

        quad_count++;
    }

    if ( quad_count == 0 ) {
        free( vertices );
        return;
    }

    // Upload vertex data with glBufferSubData (VBO is pre-allocated).
    glBindBuffer( GL_ARRAY_BUFFER, vbo_id );
    glBufferSubData( GL_ARRAY_BUFFER, 0, quad_count * 4 * sizeof( TextVertex ), vertices );
    free( vertices );

    // Draw.
    shader.bind( );
    shader.set_uniform_mat4f( "u_MVP", mvp );
    shader.set_uniform4f( "u_Tint", tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] );

    // Bind the atlas to texture unit 0.
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, atlas_texture_id );
    shader.set_uniform1i( "u_Texture", 0 );

    // Disable depth testing and face culling so text is always drawn on top.
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_CULL_FACE );

    // Draw using glDrawElements (non-instanced).
    glBindVertexArray( vao_id );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo_id );
    glDrawElements( GL_TRIANGLES, quad_count * 6, GL_UNSIGNED_INT, NULL );
    glBindVertexArray( 0 );

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    showErrors( );
}

void FontRenderer::cleanup( ) {
    glDeleteTextures( 1, &atlas_texture_id );
    glDeleteVertexArrays( 1, &vao_id );
    glDeleteBuffers( 1, &vbo_id );
    glDeleteBuffers( 1, &ibo_id );
    shader.destroy( );
}
