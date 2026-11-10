#pragma once

// Screen-space UI layout: one Clay pass produces every rect the draw and
// input code reads, replacing per-caller position re-derivation (the
// source of panel/HUD overlap regressions). Pure geometry — no GL — so
// rep_tests can exercise it across screen sizes.
//
// Clay works top-left origin, y-down, in pixels; every rect handed out
// here is converted to RepGame's centered-origin, y-up UI space (the same
// space UIOverlayInstance::screen_x/screen_y live in). The conversion is
// centralized in ui_layout.cpp so draw code never mixes conventions.

struct Clay_Context;

// One axis-aligned rect in centered, y-up UI pixels.
struct UiRect {
    float left = 0;
    float bottom = 0;
    float width = 0;
    float height = 0;

    float top( ) const {
        return this->bottom + this->height;
    }
    float right( ) const {
        return this->left + this->width;
    }
    float center_x( ) const {
        return this->left + this->width * 0.5f;
    }
    bool contains( float x, float y ) const {
        return x >= this->left && x < this->right( ) && y >= this->bottom && y < this->top( );
    }
};

// Everything one layout pass needs, kept POD so tests can build it
// without a RepGameState (ui_layout_input fills it from live state).
struct UiLayoutInput {
    float screen_w = 0;
    float screen_h = 0;
    // Hotbar items-slab size in px and its cell stride — the stride that
    // sizes the vitals icons and inter-element gaps.
    float hotbar_slab_w = 0;
    float hotbar_slab_h = 0;
    float cell_stride = 0;
    // Cap height of the selected-item label at its drawn size.
    float label_h = 0;
};

// Named regions draw/input code reads instead of re-deriving positions.
struct UiLayoutRects {
    // Hotbar items slab (slot cells, border and all).
    UiRect hotbar;
    // Hearts/bubbles strip; always reserved so panel pins survive a
    // game-mode toggle without a resize, drawn only in survival.
    UiRect vitals;
    // Reservation slot for the selected-item name — its bottom edge is
    // the text baseline in survival.
    UiRect item_label;
    // Creative-mode position for the same label (snug above the hotbar;
    // there is no hearts row to sit over).
    UiRect item_label_low;
    // Whole bottom HUD strip including top headroom: inventory panels pin
    // their bottom edge to hud_strip.top().
    UiRect hud_strip;
};

class UiLayout {
  public:
    // Allocates the Clay arena/context and binds `font` as the
    // MeasureText callback used by CLAY_TEXT elements.
    void init( class FontRenderer &font );
    void destroy( );
    // Runs the layout pass and refreshes rects(). No GL state touched.
    void compute( const UiLayoutInput &in );
    const UiLayoutRects &rects( ) const {
        return this->rects_;
    }

  private:
    void *arena_mem = nullptr;
    Clay_Context *context = nullptr;
    UiLayoutRects rects_;
};

struct RepGameState;
// Builds the UiLayoutInput for the current frame's state (reads the
// hotbar renderer's slab metrics + font metrics).
UiLayoutInput ui_layout_input( const RepGameState &state );
