#pragma once

#include "common/RenderChain.hpp"
#include "common/ui_overlay.hpp"
#include "common/ui_layout.hpp" // UiRect, UI_LAYOUT_MAX_ROWS, UI_TEXT_BASE_SIZE
#include "common/font_renderer.hpp"

struct RepGameState;

// Escape / Android-menu pause screen: a dim backdrop, a centered panel, and
// tappable buttons drawn with the flat (non-block) UIOverlay instancing
// path, laid out in lines of one or two side-by-side cells. Line contents
// are compile-time: the graphics-quality button only exists where
// SUPPORTS_FRAME_BUFFER, the ImGui one where SUPPORTS_IMGUI_OVERLAY.
//
// Geometry comes from the central layout pass (UiLayout): it declares one
// full-width row per line, and split_row below subdivides each row rect
// into the clickable button cells at draw/hit-test time.
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
        // Marks the time-of-day row; like the other stepper rows its
        // buttons return BUTTON_TIME_MINUS / _RESET / _PLUS.
        BUTTON_TIME_ROW,
        BUTTON_TIME_MINUS,
        BUTTON_TIME_PLUS,
        BUTTON_TIME_RESET,
        BUTTON_DEBUG_VIEW,
        BUTTON_GAME_MODE,
        BUTTON_PEACEFUL,
        BUTTON_FLYING,
        BUTTON_IMGUI,
        BUTTON_TOUCH,
        BUTTON_EXIT,
        NUM_BUTTONS
    };

    // One line of the menu: up to three side-by-side cells. A lone
    // stepper kind (BUTTON_*_ROW) expands to [-] [label] [+] cells via
    // stepper_ids at split time.
    struct Line {
        int kinds[ 3 ];
        int count;
    };
    static constexpr int MAX_ROWS = UI_LAYOUT_MAX_ROWS;

    // Content description consumed by the layout pass (all pure — no GL).
    static int build_lines( bool dead, Line *lines, int max_lines );
    // Longest label a button can show — the layout pass measures these so
    // buttons never resize when the live value changes.
    static const char *worst_label( int button, bool dead, char *buf, size_t buf_size );
    static bool is_stepper_row( int button );
    static void stepper_ids( int row_button, int &minus, int &reset, int &plus );

    // Splits a layout-provided row rect into its per-button cells (equal
    // widths, whole-pixel x edges — the same edges the old manual layout
    // produced). Returns the number of cells written (1..3).
    static int split_row( const Line &line, const UiRect &row, float gap, int ids[ 3 ], UiRect rects[ 3 ] );

  private:
    // Death-screen mode: layout collapses to Respawn + Exit under a
    // "You died!" title (set via set_dead from player_damage/respawn).
    bool dead_mode = false;

    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_quads;
    // Single-instance chain redrawn each frame over the hovered button
    // (desktop pointer feedback; harmless on touch since absPosition only
    // moves on touch).
    RenderChain<UIOverlayVertex, UIOverlayInstance> render_chain_hover;

  public:
    void init( const VertexBufferLayout &vbl_vertex, const VertexBufferLayout &vbl_instance );
    // Death-screen mode toggle — recomputes the layout pass immediately
    // (the row set and panel tint depend on it).
    void set_dead( RepGameState &state, bool dead );
    bool is_dead( ) const {
        return this->dead_mode;
    }
    void draw( const Renderer &renderer, Shader &shader, const Texture &blocksTexture, FontRenderer &font, const glm::mat4 &mvp, const RepGameState &state );
    // abs_x/abs_y are top-left-origin screen pixels (SDL mouse / touch).
    // Returns a BUTTON_* id or BUTTON_NONE.
    int hitTest( const RepGameState &state, int abs_x, int abs_y ) const;
    void cleanup( );
};
