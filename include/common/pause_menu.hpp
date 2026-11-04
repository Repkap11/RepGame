#pragma once

#include "common/RenderChain.hpp"
#include "common/ui_overlay.hpp"
#include "common/font_renderer.hpp"

struct RepGameState;

// Escape / Android-menu pause screen: a dim backdrop, a centered panel, and
// tappable buttons drawn with the flat (non-block) UIOverlay instancing
// path, laid out in lines of one or two side-by-side cells. Line contents
// are compile-time: the graphics-quality button only exists where
// SUPPORTS_FRAME_BUFFER, the ImGui one where SUPPORTS_IMGUI_OVERLAY.
class PauseMenu {
  public:
    enum Button {
        BUTTON_NONE = -1,
        BUTTON_RESUME = 0,
        BUTTON_GRAPHICS,
        BUTTON_SHADOW,
        // Marks the render-distance row for layout/drawing; the row's
        // buttons return BUTTON_RENDER_MINUS / _RESET / _PLUS.
        BUTTON_RENDER_ROW,
        BUTTON_RENDER_MINUS,
        BUTTON_RENDER_PLUS,
        BUTTON_RENDER_RESET,
        // Marks the cave-brightness row; like the render row its buttons
        // return BUTTON_BRIGHT_MINUS / _RESET / _PLUS.
        BUTTON_BRIGHT_ROW,
        BUTTON_BRIGHT_MINUS,
        BUTTON_BRIGHT_PLUS,
        BUTTON_BRIGHT_RESET,
        BUTTON_DEBUG_VIEW,
        BUTTON_GAME_MODE,
        BUTTON_FLYING,
        BUTTON_IMGUI,
        BUTTON_EXIT,
        NUM_BUTTONS
    };

  private:
    // A clickable rect in centered UI coords (origin screen center, y up).
    struct Region {
        int button;
        float x, y, w, h;
    };
    // Per-button label anchors parallel to the layout pass.
    struct Row {
        int button; // BUTTON_*; BUTTON_*_ROW for the -/+ stepper rows
        float center_x; // label center: cell center, or the stepper's mid button
        float center_y;
        float left_cx;  // stepper rows only: minus-button center x
        float right_cx; // stepper rows only: plus-button center x
    };
    static constexpr int MAX_ROWS = 16;

    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_quads;
    // Single-instance chain redrawn each frame over the hovered button
    // (desktop pointer feedback; harmless on touch since absPosition only
    // moves on touch).
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_hover;
    Region regions[ MAX_ROWS ];
    int num_regions = 0;
    Row rows[ MAX_ROWS ];
    int num_rows = 0;
    int screen_width = 0;
    int screen_height = 0;
    float title_center_y = 0.0f;
    float title_size = 0.0f;
    float text_size = 0.0f;

  public:
    void init( const VertexBufferLayout &vbl_vertex, const VertexBufferLayout &vbl_instance );
    // font is used to fit label sizes: text only ever draws at 13*k px
    // (FontRenderer::pixelScale), so the layout asks it for real widths.
    void onScreenSizeChange( int width, int height, const FontRenderer &font );
    void draw( const Renderer &renderer, Shader &shader, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp, const RepGameState &state );
    // abs_x/abs_y are top-left-origin screen pixels (SDL mouse / touch).
    // Returns a BUTTON_* id or BUTTON_NONE.
    int hitTest( int abs_x, int abs_y ) const;
    void cleanup( );
};
