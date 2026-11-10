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
class FontRenderer;

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
    static constexpr int UI_LAYOUT_MAX_ROWS = 24;
    UiRect pause_rows[ UI_LAYOUT_MAX_ROWS ];
    int pause_row_count = 0;
    // Inter-row/cell gap and the fit-solved integer text scale: button
    // labels draw at 13*pause_k px, the title at 13*(pause_k+1).
    float pause_gap = 0;
    int pause_k = 0;
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
    void *arena_mem = nullptr;
    Clay_Context *context = nullptr;
    // Kept for content measurement during layout (worst-case labels etc.).
    FontRenderer *font_ = nullptr;
    UiLayoutRects rects_;
};

struct RepGameState;
// Builds the UiLayoutInput for the current frame's state (reads the
// hotbar renderer's slab metrics + font metrics).
UiLayoutInput ui_layout_input( const RepGameState &state );
