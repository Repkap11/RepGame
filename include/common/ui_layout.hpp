#pragma once

#include <stdint.h>

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
class FontRenderer;

// Selected-item label pixel size — the layout pass reserves a slot this
// tall (label_h is its cap height) and game_render draws the label at it.
constexpr float UI_ITEM_LABEL_SIZE = 22.0f;
// The font's native 13px raster: pause-menu text draws at integer
// multiples (button labels kx, the title (k+1)x) so it stays pixel-crisp.
constexpr float UI_TEXT_BASE_SIZE = 13.0f;
// Pause-menu row capacity — PauseMenu::MAX_ROWS aliases this (one value
// owns the row array size in UiLayoutRects and the Line buffers).
constexpr int UI_LAYOUT_MAX_ROWS = 24;

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
    float center_y( ) const {
        return this->bottom + this->height * 0.5f;
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
    // Survival inventory items-slab size (from its compute_layout).
    float surv_slab_w = 0;
    float surv_slab_h = 0;
    // Survival cell stride — the shared grid the container screens draw
    // their cells at (fixed_cell_stride panels).
    float surv_stride = 0;
    // Container-screen slab sizes measured off each panel's
    // InventoryRenderer items rect (stride*dim + border) — read, never
    // re-derived here, so the border math stays renderer-owned.
    float chest_slab_w = 0;
    float chest_slab_h = 0;
    // Square slabs at the survival stride: the 2x2/3x3 crafting grids and
    // output cell, and the furnace cells (input/output/fuel are all 1x1).
    float craft_grid2_slab = 0;
    float craft_grid3_slab = 0;
    float craft_out_slab = 0;
    float furnace_slab = 0;
    // Creative palette: items slab, the (undrawn) panel bounds the search
    // bar anchors to, and the cell stride that sizes the search bar.
    float creative_slab_w = 0;
    float creative_slab_h = 0;
    float creative_panel_w = 0;
    float creative_panel_h = 0;
    float creative_stride = 0;
    // Lowest y (centered coords) the creative panel bottom may sit at —
    // its bottom_reserved_percent floor.
    float creative_floor = 0;
    // Pause-menu content variant: the death screen collapses to
    // Respawn + Exit under "You died!".
    bool pause_dead = false;
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

    // Survival inventory items slab — bottom edge at the higher of the
    // creative panel's bottom or the HUD strip top (matching the old
    // surv_pin rule), horizontally centered. Container screens stack
    // above it inside the same group.
    UiRect survival_items;
    // Creative palette window bounds (not drawn — the search bar anchors
    // to its top edge and its bottom edge feeds the survival pin).
    UiRect creative_panel;
    // Creative palette items slab, bottom-anchored in the panel.
    UiRect creative_items;
    // Creative search bar, floating above the panel's top edge.
    UiRect search_box;

    // Container screens, all floating above the survival slab (every
    // rect is always computed — the draw pass decides which screen is
    // actually open, so nothing needs a relayout on open/close).
    // Chest: one 9x3 slab.
    UiRect chest_items;
    // Crafting: the 2x2 and 3x3 variants side by side — grid, the
    // one-stride arrow gap, the output cell.
    UiRect crafting_grid2, crafting_out2, crafting_gap2;
    UiRect crafting_grid3, crafting_out3, crafting_gap3;
    // Furnace: input + output on the top row (furnace_mid is the text
    // gap between them), fuel centered below.
    UiRect furnace_input, furnace_mid, furnace_output, furnace_fuel;
    // Window backdrops: each frames its screen's panels plus the survival
    // slab with a 0.35-stride pad — one unified window.
    UiRect chest_backdrop;
    UiRect crafting_backdrop2, crafting_backdrop3;
    UiRect furnace_backdrop;

    // Pause menu (modal, centered; always laid out, drawn/hit-tested only
    // while open). pause_rows[i] is the full-width band of line i;
    // PauseMenu::split_row subdivides it into equal button cells.
    UiRect pause_panel;
    // Title band (one cap-height tall); its bottom edge is the baseline.
    UiRect pause_title;
    UiRect pause_rows[ UI_LAYOUT_MAX_ROWS ];
    int pause_row_count = 0;
    // Inter-row/cell gap and the fit-solved integer text scale: button
    // labels draw at 13*pause_k px, the title at 13*(pause_k+1).
    float pause_gap = 0;
    int pause_k = 0;

    // Bitmask of UiOverflow — which screens exceeded the viewport this
    // pass. Overflow is possible (a window can be smaller than a screen
    // needs) but inspectable: the draw code still runs, and tests/debug
    // runs can assert it is 0 at supported sizes.
    unsigned overflow = 0;
};

// Viewport-overflow flags reported in UiLayoutRects::overflow.
enum UiOverflow : unsigned {
    UI_OVERFLOW_NONE = 0,
    UI_OVERFLOW_HUD = 1u << 0,
    UI_OVERFLOW_SURVIVAL = 1u << 1,
    UI_OVERFLOW_CREATIVE = 1u << 2,
    UI_OVERFLOW_CHEST = 1u << 3,
    UI_OVERFLOW_CRAFTING2 = 1u << 4,
    UI_OVERFLOW_CRAFTING3 = 1u << 5,
    UI_OVERFLOW_FURNACE = 1u << 6,
    UI_OVERFLOW_PAUSE = 1u << 7,
};

// 16:9-normalized height basis shared by UI sizing: the UI stays a
// constant fraction of screen width on displays wider than 16:9, so
// controls don't shrink on ultrawide phones.
inline float ui_layout_height_basis( float w, float h ) {
    const float basis = w * ( 9.0f / 16.0f );
    return h > basis ? h : basis;
}

class UiLayout {
  public:
    // Allocates the Clay arena/context and binds `font` as the
    // MeasureText callback used by CLAY_TEXT elements.
    void init( FontRenderer &font );
    void destroy( );
    // Runs the layout pass and refreshes rects(). No GL state touched.
    void compute( const UiLayoutInput &in );
    const UiLayoutRects &rects( ) const {
        return this->rects_;
    }

  private:
    // Fills rects_.overflow and warns (once per newly-overflowing screen)
    // when emitted rects escape the viewport or overlap their siblings.
    // strip_h is the precomputed HUD-strip height the pin math assumed —
    // validated against the measured strip since it duplicates the
    // declared children's sum.
    void validate( const UiLayoutInput &in, float strip_h );
    void *arena_mem = nullptr;
    Clay_Context *context = nullptr;
    // Kept for content measurement during layout (worst-case labels etc.).
    FontRenderer *font_ = nullptr;
    UiLayoutRects rects_;
    // Last pass's overflow mask — warnings log only on newly-set bits so
    // a window drag doesn't spam one line per resize event.
    unsigned last_overflow_ = 0;
    uint64_t last_overlap_ = 0;
};

struct RepGameState;
// Builds the UiLayoutInput for the current frame's state (reads the
// hotbar renderer's slab metrics + font metrics).
UiLayoutInput ui_layout_input( const RepGameState &state );
