#include <limits.h>
#include <math.h>
#include <string.h>

#include "common/RepGame.hpp"
#include "common/font_renderer.hpp"
#include "common/renderer/shader.hpp"
#include "common/vendor/stb_truetype.hpp"

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_ANDROID )
MK_BLOB( fonts, proggyclean, ttf );
static const unsigned char *get_font_data( int *out_size ) {
    *out_size = proggyclean.length;
    return reinterpret_cast<const unsigned char *>( proggyclean.source );
}
#elif defined( REPGAME_WASM )
#include <cstdio>
static unsigned char *wasm_font_data = nullptr;
static int wasm_font_size = 0;
static const unsigned char *get_font_data( int *out_size ) {
    if ( !wasm_font_data ) {
        FILE *f = fopen( "fonts/proggyclean.ttf", "rb" );
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
#endif

MK_SHADER( text_vertex );
MK_SHADER( text_fragment );

// Gap between adjacent glyphs' ink, in raster texels (scaled by k with
// everything else): each glyph advances by its ink width plus this.
static constexpr int kLetterSpacing = 1;
// Pen step for glyphs with no ink (space): the original cell was 7
// texels; 4 reads as a word gap in a proportional pixel font.
static constexpr int kSpaceAdvance = 4;

// Interleaved vertex: position (x,y) + UV (u,v) + per-vertex tint per corner.
// The tint is per-vertex (not a uniform) so one batched draw can mix colours.
struct TextVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
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

    this->font_size = 13.0f;
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
    constexpr int kAtlasWidth = 256;
    constexpr int kAtlasHeight = 128;
    constexpr size_t kAtlasPixels = static_cast<size_t>( kAtlasWidth ) * kAtlasHeight;
    this->atlas_width = kAtlasWidth;
    this->atlas_height = kAtlasHeight;

    unsigned char *atlas_pixels = static_cast<unsigned char *>( calloc( kAtlasPixels, 1 ) );

    stbtt_pack_context pack_ctx;
    int pack_ok = stbtt_PackBegin( &pack_ctx, atlas_pixels, atlas_width, atlas_height, 0, 1, nullptr );
    if ( !pack_ok ) {
        pr_debug( "FontRenderer: stbtt_PackBegin failed" );
        free( atlas_pixels );
        return;
    }

    const int packed = stbtt_PackFontRange( &pack_ctx, font_data, 0, font_size, first_char, num_chars, glyphs );
    stbtt_PackEnd( &pack_ctx );
    if ( !packed ) {
        // Some glyphs didn't fit; they'd render as empty quads.
        pr_debug( "FontRenderer: pack failed for %d glyphs into %dx%d atlas", num_chars, atlas_width, atlas_height );
    }

    // Crisp 1-bit font: threshold the rasterized coverage to hard on/off so
    // no partially-covered (gray) pixels survive.
    for ( int i = 0; i < atlas_width * atlas_height; i++ ) {
        atlas_pixels[ i ] = atlas_pixels[ i ] >= 128 ? 255 : 0;
    }

    // Proportional metrics: each glyph's leftmost/rightmost lit column in
    // the same pen-relative integer geometry text_append uses
    // (align_to_integer=1 from pen 0,0, so the bitmap lands at round(xoff)
    // relative to the pen). The quad is shifted so ink starts at the pen
    // and the pen steps by ink width + kLetterSpacing; glyphs with no ink
    // (space) step by kSpaceAdvance.
    for ( int i = 0; i < num_chars; i++ ) {
        const stbtt_packedchar &g = glyphs[ i ];
        const int qx0 = static_cast<int>( floorf( g.xoff + 0.5f ) );
        int min_col = INT_MAX, max_col = INT_MIN;
        for ( int y = g.y0; y < g.y1; y++ ) {
            for ( int x = g.x0; x < g.x1; x++ ) {
                if ( atlas_pixels[ y * atlas_width + x ] ) {
                    const int col = qx0 + ( x - g.x0 );
                    if ( col < min_col ) min_col = col;
                    if ( col > max_col ) max_col = col;
                }
            }
        }
        this->glyph_has_ink[ i ] = min_col != INT_MAX;
        this->glyph_shift[ i ] = min_col == INT_MAX ? 0 : -min_col;
        this->glyph_advance[ i ] = min_col == INT_MAX ? kSpaceAdvance : max_col - min_col + 1 + kLetterSpacing;
    }

    // Convert the single-channel atlas to RGBA for maximum driver compatibility.
    unsigned char *atlas_rgba = static_cast<unsigned char *>( malloc( kAtlasPixels * 4 ) );
    for ( size_t i = 0; i < kAtlasPixels; i++ ) {
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
    // Tint attribute (location 2): 4 floats
    glEnableVertexAttribArray( 2 );
    glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, sizeof( TextVertex ), ( const void * )( 4 * sizeof( float ) ) );

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

int FontRenderer::pixelScale( float size ) const {
    int k = static_cast<int>( lroundf( size / font_size ) );
    return k < 1 ? 1 : k;
}

void FontRenderer::onScreenSizeChange( int w, int h ) {
    this->screen_width = w;
    this->screen_height = h;
}

float FontRenderer::getTextWidthSlice( const char *text, int len, float size ) const {
    // Width is the sum of proportional pen steps, matching text_append, so
    // measured and drawn widths always agree. The last glyph's trailing
    // ink gap is excluded so the result is the visible extent (keeps
    // centred/right-aligned text exact).
    const int k = pixelScale( size );
    float width = 0;
    int last_idx = -1;
    for ( const char *p = text, *end = text + len; p < end && *p; p++ ) {
        int codepoint = static_cast<unsigned char>( *p );
        if ( codepoint < first_char || codepoint >= first_char + num_chars ) continue;
        const int cur_idx = codepoint - first_char;
        width += this->glyph_advance[ cur_idx ];
        last_idx = cur_idx;
    }
    if ( last_idx >= 0 && this->glyph_has_ink[ last_idx ] ) {
        width -= kLetterSpacing;
    }
    return width * k;
}

float FontRenderer::getTextWidth( const char *text, float size ) const {
    return this->getTextWidthSlice( text, static_cast<int>( strlen( text ) ), size );
}

void FontRenderer::measureSlice( const char *text, int len, float size, float &out_w, float &out_h ) const {
    out_w = this->getTextWidthSlice( text, len, size );
    out_h = this->getAscent( size ) - this->getDescent( size );
}

float FontRenderer::getAscent( float size ) const {
    return roundf( ascent ) * pixelScale( size );
}

float FontRenderer::getDescent( float size ) const {
    return roundf( descent ) * pixelScale( size );
}

float FontRenderer::getCapHeight( float size ) const {
    // yoff is the glyph bitmap's top bearing above the pen baseline
    // (negative for a capital letter that rises above it).
    if ( 'H' < first_char || 'H' >= first_char + num_chars ) {
        return getAscent( size );
    }
    return roundf( -glyphs[ 'H' - first_char ].yoff ) * pixelScale( size );
}

void FontRenderer::text_begin( ) {
    this->batch_quads = 0;
}

void FontRenderer::text_append( const char *text, float x, float y, float size, const float tint[ 4 ] ) {
    this->text_append_slice( text, static_cast<int>( strlen( text ) ), x, y, size, tint );
}

void FontRenderer::text_append_slice( const char *text, int len, float x, float y, float size, const float tint[ 4 ] ) {
    const int k = pixelScale( size );
    float pen_x = 0;
    float pen_y = 0;

    // Snap the draw origin onto the screen pixel grid. screen.ortho_center's
    // origin is the screen center, so on odd-sized screens plain integer x/y
    // is already off by half a pixel; floor(x + w/2) - w/2 lands on a real
    // texel boundary either way.
    const float ox = floorf( x + this->screen_width * 0.5f ) - this->screen_width * 0.5f;
    const float oy = floorf( y + this->screen_height * 0.5f ) - this->screen_height * 0.5f;

    // Grow the staging buffer to fit this string's worst case (one quad per
    // char); each append at least doubles so growth amortizes.
    const int needed = this->batch_quads + len;
    if ( needed > this->batch_capacity ) {
        int capacity = this->batch_capacity > 0 ? this->batch_capacity : 256;
        while ( capacity < needed ) {
            capacity *= 2;
        }
        this->batch = static_cast<TextVertex *>( realloc( this->batch, static_cast<size_t>( capacity ) * 4 * sizeof( TextVertex ) ) );
        this->batch_capacity = capacity;
    }
    TextVertex *vertices = this->batch + this->batch_quads * 4;
    int quad_count = 0;

    // Build vertex data for all glyphs using stbtt_GetPackedQuad.
    // We render at the native font_size (pen starts at 0,0), then scale
    // and translate the final quad positions to (x, y) at the requested size.
    for ( const char *p = text, *end = text + len; p < end && *p; p++ ) {
        int codepoint = static_cast<unsigned char>( *p );
        if ( codepoint < first_char || codepoint >= first_char + num_chars ) continue;
        const int cur_idx = codepoint - first_char;

        stbtt_aligned_quad q;
        const float pen_start = pen_x;
        // align_to_integer=1 keeps every quad corner integer at the raster
        // size; glyph_shift then moves the ink so its leftmost column sits
        // at the pen, and the pen steps by the proportional advance — all
        // integer texel counts, so nothing drifts off the pixel grid.
        stbtt_GetPackedQuad( glyphs, atlas_width, atlas_height, cur_idx, &pen_x, &pen_y, &q, 1 );
        q.x0 += this->glyph_shift[ cur_idx ];
        q.x1 += this->glyph_shift[ cur_idx ];
        pen_x = pen_start + this->glyph_advance[ cur_idx ];

        // Scale quad positions from native font_size to requested size,
        // and translate to (x, y). Every offset is an integer number of
        // raster texels, so each font texel covers a k×k pixel block.
        // stbtt uses y-down coordinates: y0=top (negative, above pen),
        // y1=bottom (positive, below pen). Our screen is y-up, so flip Y.
        float sx0 = ox + q.x0 * k;
        float sy0 = oy - q.y0 * k;  // top of glyph (above baseline)
        float sx1 = ox + q.x1 * k;
        float sy1 = oy - q.y1 * k;  // bottom of glyph (below baseline)

        TextVertex *quad = &vertices[ quad_count * 4 ];
        // stbtt uses y-down: y0=top (negative, above pen), y1=bottom (positive, below pen).
        // Our screen is y-up, so flip Y: sy0 = top (higher y), sy1 = bottom (lower y).
        // stbtt texture coords: t0=top of glyph in atlas, t1=bottom.
        // glTexImage2D puts atlas row 0 at v=0 (bottom of texture in OpenGL),
        // so the atlas is flipped in the texture. Swap t0/t1 to compensate.
        // Bottom-left (lower y on screen = bottom of glyph)
        quad[ 0 ] = { sx0, sy1, q.s0, q.t1, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
        // Top-left (higher y on screen = top of glyph)
        quad[ 1 ] = { sx0, sy0, q.s0, q.t0, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
        // Top-right (higher y on screen = top of glyph)
        quad[ 2 ] = { sx1, sy0, q.s1, q.t0, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };
        // Bottom-right (lower y on screen = bottom of glyph)
        quad[ 3 ] = { sx1, sy1, q.s1, q.t1, tint[ 0 ], tint[ 1 ], tint[ 2 ], tint[ 3 ] };

        quad_count++;
    }
    this->batch_quads += quad_count;
}

void FontRenderer::text_flush( const Renderer &renderer, const glm::mat4 &mvp ) {
    if ( this->batch_quads == 0 ) {
        return;
    }
    shader.bind( );
    shader.set_uniform_mat4f( "u_MVP", mvp );

    // Bind the atlas to texture unit 0.
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, atlas_texture_id );
    shader.set_uniform1i( "u_Texture", 0 );

    // Disable depth testing and face culling so text is always drawn on top.
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_CULL_FACE );

    glBindVertexArray( vao_id );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo_id );
    glBindBuffer( GL_ARRAY_BUFFER, vbo_id );
    // Upload + draw in max_quads chunks — the VBO is a fixed-size staging
    // buffer while batch is unbounded.
    for ( int base = 0; base < this->batch_quads; base += max_quads ) {
        const int count = this->batch_quads - base < max_quads ? this->batch_quads - base : max_quads;
        glBufferSubData( GL_ARRAY_BUFFER, 0, count * 4 * sizeof( TextVertex ), this->batch + base * 4 );
        glDrawElements( GL_TRIANGLES, count * 6, GL_UNSIGNED_INT, NULL );
    }
    glBindVertexArray( 0 );

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    this->batch_quads = 0;
    showErrors( );
}

void FontRenderer::text_append_outlined( const char *text, float x, float y, float size, const float tint[ 4 ] ) {
    const float k = static_cast<float>( pixelScale( size ) );
    const float black[ 4 ] = { 0.0f, 0.0f, 0.0f, tint[ 3 ] };
    for ( int dy = -1; dy <= 1; dy++ ) {
        for ( int dx = -1; dx <= 1; dx++ ) {
            if ( dx || dy ) {
                this->text_append( text, x + dx * k, y + dy * k, size, black );
            }
        }
    }
    this->text_append( text, x, y, size, tint );
}

void FontRenderer::cleanup( ) {
    glDeleteTextures( 1, &atlas_texture_id );
    glDeleteVertexArrays( 1, &vao_id );
    glDeleteBuffers( 1, &vbo_id );
    glDeleteBuffers( 1, &ibo_id );
    shader.destroy( );
}
