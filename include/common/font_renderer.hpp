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

    // Glyph metrics for ASCII 32..126. Value-initialized so an uninit'd
    // FontRenderer measures deterministically (all zeros) — the Clay
    // layout tests bind one without ever calling init().
    stbtt_packedchar glyphs[ 95 ] = { };
    int first_char = 32;
    int num_chars = 95;

    // Proportional metrics computed at init from the thresholded atlas.
    // glyph_shift repositions the quad so the leftmost ink column sits at
    // the pen; glyph_advance is the pen step (ink width + kLetterSpacing,
    // kSpaceAdvance for glyphs with no ink like space).
    int glyph_shift[ 95 ] = { };
    int glyph_advance[ 95 ] = { };
    bool glyph_has_ink[ 95 ] = { };

    float font_size = 13.0f; // rasterized pixel size
    float ascent = 0.0f;
    float descent = 0.0f;
    float line_gap = 0.0f;

    // Raw GL buffers for dynamic per-frame text quads
    unsigned int vao_id;
    unsigned int vbo_id;
    unsigned int ibo_id;
    int max_quads;

    // CPU-side staging for batched text: text_append builds glyph quads
    // here; text_flush uploads and draws them in one call. Owned by
    // text_begin (reallocs as needed).
    struct TextVertex *batch = nullptr;
    int batch_capacity = 0;  // in quads
    int batch_quads = 0;

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
    // Batched drawing: text_begin resets the staging buffer, text_append /
    // text_append_slice queue glyph quads (no GL work), text_flush uploads
    // and draws everything queued so far in one call. text_append_outlined
    // queues a one-raster-texel black outline (8 offset passes under the
    // tinted text) so it reads against any background.
    void text_begin( );
    void text_append( const char *text, float x, float y, float size, const float tint[ 4 ] );
    void text_append_outlined( const char *text, float x, float y, float size, const float tint[ 4 ] );
    void text_append_slice( const char *text, int len, float x, float y, float size, const float tint[ 4 ] );
    void text_flush( const Renderer &renderer, const glm::mat4 &mvp );
    float getTextWidth( const char *text, float size ) const;
    // getTextWidth for a non-null-terminated slice (Clay StringSlice).
    float getTextWidthSlice( const char *text, int len, float size ) const;
    // Width + line-box height of a slice — the Clay MeasureText shape.
    void measureSlice( const char *text, int len, float size, float &out_w, float &out_h ) const;
    float getAscent( float size ) const;
    float getDescent( float size ) const;
    // Capital-letter height (top of 'H' above baseline) at the snapped scale;
    // used to center text by its visible cap height rather than the ascent.
    float getCapHeight( float size ) const;
    void cleanup( );
};
