#pragma once

#include "common/renderer/shader.hpp"
#include "common/renderer/renderer.hpp"
#include "binary_blobs.hpp"
#include "common/vendor/stb_truetype.hpp"

// Per-glyph metrics stored after rasterizing the font atlas.
struct GlyphInfo {
    float x0, y0; // top-left UV in atlas
    float x1, y1; // bottom-right UV in atlas
    float xoff;   // bitmap left bearing relative to pen position (pixels)
    float yoff;   // bitmap top bearing relative to pen baseline (pixels, positive = above)
    float xoff2;  // bitmap right edge relative to pen position (pixels)
    float yoff2;  // bitmap bottom edge relative to pen baseline (pixels)
    float xadvance; // horizontal advance to next glyph, in pixels
};

class FontRenderer {
    Shader shader;
    unsigned int atlas_texture_id;
    int atlas_width;
    int atlas_height;

    // Glyph metrics for ASCII 32..126
    stbtt_packedchar glyphs[ 95 ];
    int first_char;
    int num_chars;

    float font_size; // rasterized pixel size
    float ascent;
    float descent;
    float line_gap;

    // Raw GL buffers for dynamic per-frame text quads
    unsigned int vao_id;
    unsigned int vbo_id;
    unsigned int ibo_id;
    int max_quads;

    // Screen size, needed to snap text origins to the pixel grid (the UI
    // ortho origin is the screen center, so odd dimensions offset by half).
    int screen_width = 0;
    int screen_height = 0;

  public:
    void init( );
    void onScreenSizeChange( int w, int h );
    // Integer magnification for a requested pixel size: the 13px raster is
    // drawn at k× nearest-neighbour scale so text stays pixel-crisp.
    int pixelScale( float size ) const;
    void drawString( const Renderer &renderer, const char *text, float x, float y, float size, const float tint[ 4 ], const glm::mat4 &mvp );
    float getTextWidth( const char *text, float size ) const;
    float getAscent( float size ) const;
    float getDescent( float size ) const;
    // Capital-letter height (top of 'H' above baseline) at the snapped scale;
    // used to center text by its visible cap height rather than the ascent.
    float getCapHeight( float size ) const;
    void cleanup( );
};
