// The Clay-backed UI layout pass: HUD-strip + pause-menu rect geometry
// across aspect ratios. Runs without GL — the FontRenderer here is
// uninit'd, so text measures as zero (deterministic) and only the
// panel/row structure is asserted, not label widths.

#include "test_util.hpp"
#include "common/font_renderer.hpp"
#include "common/ui_layout.hpp"

static int check_strip( UiLayout &layout, const char *name, float w, float h, float slab_w, float stride, unsigned want_overflow, bool check_positions = true ) {
    int failures = 0;
    UiLayoutInput in;
    in.screen_w = w;
    in.screen_h = h;
    in.hotbar_slab_w = slab_w;
    in.hotbar_slab_h = stride + 5.0f;
    in.cell_stride = stride;
    in.label_h = 18.0f;
    // Inventory slabs mirror compute_layout's outputs: survival is a 9x3
    // grid at its own stride; the creative palette is screen-fraction
    // sized with a 0.1*basis bottom reservation and a height-limited
    // (0.75*panel) items slab.
    in.surv_slab_w = 9.1f * stride;
    in.surv_slab_h = 3.1f * stride;
    in.surv_stride = stride;
    // Container slab dims mirror what the panels' measured items rects
    // produce (stride*dim + a 0.1-stride border).
    in.chest_slab_w = 9.1f * stride;
    in.chest_slab_h = 3.1f * stride;
    in.craft_grid2_slab = 2.1f * stride;
    in.craft_grid3_slab = 3.1f * stride;
    in.craft_out_slab = 1.1f * stride;
    in.furnace_slab = 1.1f * stride;
    in.creative_slab_w = fminf( w * 0.7f, 720.0f );
    in.creative_panel_w = w * 0.75f;
    const float basis = fmaxf( h, w * ( 9.0f / 16.0f ) );
    in.creative_panel_h = fminf( 0.75f * basis, h );
    in.creative_slab_h = fminf( 6.1f * 60.0f, 0.75f * in.creative_panel_h );
    in.creative_stride = in.creative_slab_h / 6.1f;
    in.creative_floor = -h * 0.5f + 0.1f * basis + 6.0f;
    layout.compute( in );
    const UiLayoutRects &r = layout.rects( );

    const float s = in.cell_stride;
    const float eps = 0.01f;
    auto near_to = [ & ]( const char *what, float got, float want ) {
        const bool ok = fabsf( got - want ) <= eps;
        pr_test( "  %-58s got=%-9.3f want=%-9.3f  %s", what, got, want, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };
    auto is_true = [ & ]( const char *what, bool ok ) {
        pr_test( "  %-58s %s", what, ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    };

    char what[ 128 ];
    snprintf( what, sizeof( what ), "%s: overflow mask (want %#x)", name, want_overflow );
    near_to( what, static_cast<float>( r.overflow ), static_cast<float>( want_overflow ) );
    if ( !check_positions ) {
        // Degenerate sizes: Clay clamps/repositions rather than centers —
        // only the overflow flags and rect finiteness are meaningful.
        for ( const UiRect *q : { &r.hotbar, &r.survival_items, &r.chest_items, &r.pause_panel } ) {
            is_true( "extreme: rects finite", std::isfinite( q->left ) && std::isfinite( q->bottom ) && std::isfinite( q->width ) && std::isfinite( q->height ) );
        }
        return failures;
    }
    snprintf( what, sizeof( what ), "%s: hotbar slab flush bottom-center", name );
    near_to( what, r.hotbar.bottom, -h * 0.5f );
    snprintf( what, sizeof( what ), "%s: hotbar slab centered", name );
    near_to( what, r.hotbar.left, -in.hotbar_slab_w * 0.5f );
    snprintf( what, sizeof( what ), "%s: vitals row sits on the gap", name );
    near_to( what, r.vitals.bottom, r.hotbar.top( ) + 0.15f * s );
    snprintf( what, sizeof( what ), "%s: vitals icon height = 0.5 stride", name );
    near_to( what, r.vitals.height, 0.5f * s );
    snprintf( what, sizeof( what ), "%s: vitals spans the slab", name );
    near_to( what, r.vitals.left, r.hotbar.left );
    snprintf( what, sizeof( what ), "%s: label baseline over vitals + gap", name );
    near_to( what, r.item_label.bottom, r.vitals.top( ) + 0.15f * s + 4.0f );
    snprintf( what, sizeof( what ), "%s: creative label hugs the hotbar", name );
    near_to( what, r.item_label_low.bottom, r.hotbar.top( ) + 0.035f * s + 8.0f );
    snprintf( what, sizeof( what ), "%s: strip top clears the label + pad", name );
    near_to( what, r.hud_strip.top( ), r.item_label.top( ) + 0.3f * s );

    snprintf( what, sizeof( what ), "%s: hotbar under vitals", name );
    is_true( what, r.hotbar.top( ) <= r.vitals.bottom + eps );
    snprintf( what, sizeof( what ), "%s: vitals under label", name );
    is_true( what, r.vitals.top( ) <= r.item_label.bottom + eps );
    snprintf( what, sizeof( what ), "%s: strip inside viewport", name );
    is_true( what, r.hud_strip.bottom >= -h * 0.5f - eps && r.hud_strip.left >= -w * 0.5f - eps && r.hud_strip.right( ) <= w * 0.5f + eps );

    // Inventory panels: the survival slab pins to the higher of the
    // creative panel's bottom or the HUD strip's top; the palette is
    // centered + floor-lifted with the search bar above it.
    snprintf( what, sizeof( what ), "%s: survival slab centered", name );
    near_to( what, r.survival_items.left, -in.surv_slab_w * 0.5f );
    snprintf( what, sizeof( what ), "%s: creative panel centered", name );
    near_to( what, r.creative_panel.left, -in.creative_panel_w * 0.5f );
    snprintf( what, sizeof( what ), "%s: creative panel bottom = center+floor", name );
    near_to( what, r.creative_panel.bottom, fmaxf( -in.creative_panel_h * 0.5f, in.creative_floor ) );
    snprintf( what, sizeof( what ), "%s: creative slab hugs panel bottom", name );
    near_to( what, r.creative_items.bottom, r.creative_panel.bottom );
    snprintf( what, sizeof( what ), "%s: creative slab centered in panel", name );
    near_to( what, r.creative_items.center_x( ), r.creative_panel.center_x( ) );
    snprintf( what, sizeof( what ), "%s: survival pins above strip/creative", name );
    near_to( what, r.survival_items.bottom, fmaxf( r.creative_panel.bottom, r.hud_strip.top( ) ) );
    snprintf( what, sizeof( what ), "%s: search bar over slab top + gap", name );
    near_to( what, r.search_box.bottom, r.creative_items.top( ) + 0.15f * in.creative_stride );
    snprintf( what, sizeof( what ), "%s: search bar sized by stride", name );
    near_to( what, r.search_box.height, 0.55f * in.creative_stride );
    snprintf( what, sizeof( what ), "%s: search bar spans the panel", name );
    near_to( what, r.search_box.left, r.creative_panel.left );
    near_to( what, r.search_box.width, r.creative_panel.width );

    // Container screens float off the survival slab's top edge, all drawn
    // at the survival stride (slab = stride*dim + 0.1*stride border).
    const float s2 = in.surv_stride;
    const float surv_top = r.survival_items.top( );
    snprintf( what, sizeof( what ), "%s: chest bottom pins above survival", name );
    near_to( what, r.chest_items.bottom, surv_top + 0.25f * s2 );
    snprintf( what, sizeof( what ), "%s: chest slab centered", name );
    near_to( what, r.chest_items.center_x( ), r.survival_items.center_x( ) );
    snprintf( what, sizeof( what ), "%s: chest slab is 9x3 at stride", name );
    near_to( what, r.chest_items.width, 9.1f * s2 );
    near_to( what, r.chest_items.height, 3.1f * s2 );
    // Crafting rows: grid | 1-stride arrow gap | output, row centered.
    for ( int dim = 2; dim <= 3; dim++ ) {
        const UiRect &g = dim == 2 ? r.crafting_grid2 : r.crafting_grid3;
        const UiRect &gap = dim == 2 ? r.crafting_gap2 : r.crafting_gap3;
        const UiRect &out = dim == 2 ? r.crafting_out2 : r.crafting_out3;
        const float group_w = ( dim + 0.1f ) * s2 + s2 + 1.1f * s2;
        snprintf( what, sizeof( what ), "%s: crafting %dx%d row centered", name, dim, dim );
        near_to( what, g.left, -group_w * 0.5f );
        snprintf( what, sizeof( what ), "%s: crafting %dx%d row above survival", name, dim, dim );
        near_to( what, g.bottom, surv_top + 0.25f * s2 );
        snprintf( what, sizeof( what ), "%s: crafting %dx%d gap is one stride", name, dim, dim );
        near_to( what, gap.width, s2 );
        near_to( what, gap.left, g.right( ) );
        snprintf( what, sizeof( what ), "%s: crafting %dx%d output right of gap", name, dim, dim );
        near_to( what, out.left, gap.right( ) );
        snprintf( what, sizeof( what ), "%s: crafting %dx%d output centers on grid", name, dim, dim );
        near_to( what, out.center_y( ), g.center_y( ) );
    }
    // Furnace: input | mid | output row with fuel centered below.
    snprintf( what, sizeof( what ), "%s: furnace fuel centered below", name );
    near_to( what, r.furnace_fuel.center_x( ), 0.0f );
    near_to( what, r.furnace_fuel.bottom, surv_top + 1.15f * s2 );
    snprintf( what, sizeof( what ), "%s: furnace in/out row above fuel", name );
    near_to( what, r.furnace_input.bottom, surv_top + 2.45f * s2 );
    near_to( what, r.furnace_output.bottom, surv_top + 2.45f * s2 );
    snprintf( what, sizeof( what ), "%s: furnace input/output mirrored", name );
    near_to( what, r.furnace_input.center_x( ), -1.45f * s2 );
    near_to( what, r.furnace_output.center_x( ), 1.45f * s2 );
    snprintf( what, sizeof( what ), "%s: furnace mid spans the gap", name );
    near_to( what, r.furnace_mid.left, r.furnace_input.right( ) );
    near_to( what, r.furnace_mid.right( ), r.furnace_output.left );
    // Backdrops: padded unions of each screen's panels + the survival slab.
    const float bp = 0.35f * s2;
    snprintf( what, sizeof( what ), "%s: chest backdrop unions + pads", name );
    near_to( what, r.chest_backdrop.bottom, r.survival_items.bottom - bp );
    near_to( what, r.chest_backdrop.top( ), r.chest_items.top( ) + bp );
    near_to( what, r.chest_backdrop.left, fminf( r.chest_items.left, r.survival_items.left ) - bp );
    snprintf( what, sizeof( what ), "%s: crafting3 backdrop tops grid", name );
    near_to( what, r.crafting_backdrop3.top( ), r.crafting_grid3.top( ) + bp );
    snprintf( what, sizeof( what ), "%s: furnace backdrop tops in/out row", name );
    near_to( what, r.furnace_backdrop.top( ), r.furnace_input.top( ) + bp );
    snprintf( what, sizeof( what ), "%s: furnace backdrop covers fuel", name );
    is_true( what, r.furnace_backdrop.left <= r.furnace_fuel.left && r.furnace_backdrop.bottom <= r.furnace_fuel.bottom );

    // Pause menu: floating centered panel of stacked rows.
    snprintf( what, sizeof( what ), "%s: pause panel centered", name );
    near_to( what, r.pause_panel.center_x( ), 0.0f );
    snprintf( what, sizeof( what ), "%s: pause panel vertically centered", name );
    near_to( what, r.pause_panel.center_y( ), 0.0f );
    snprintf( what, sizeof( what ), "%s: pause text scale sane", name );
    is_true( what, r.pause_k >= 1 );
    snprintf( what, sizeof( what ), "%s: pause rows laid out", name );
    is_true( what, r.pause_row_count > 0 && r.pause_row_count <= UI_LAYOUT_MAX_ROWS );
    snprintf( what, sizeof( what ), "%s: title above first row", name );
    is_true( what, r.pause_title.bottom >= r.pause_rows[ 0 ].top( ) - eps );
    for ( int i = 0; i < r.pause_row_count; i++ ) {
        snprintf( what, sizeof( what ), "%s: row %d inside panel", name, i );
        const UiRect &row = r.pause_rows[ i ];
        is_true( what, row.left >= r.pause_panel.left - eps && row.right( ) <= r.pause_panel.right( ) + eps &&
                       row.bottom >= r.pause_panel.bottom - eps && row.top( ) <= r.pause_panel.top( ) + eps );
        if ( i > 0 ) {
            snprintf( what, sizeof( what ), "%s: row %d below row %d", name, i, i - 1 );
            is_true( what, row.top( ) <= r.pause_rows[ i - 1 ].bottom + eps );
        }
    }
    return failures;
}

int test_ui_layout( ) {
    UiLayout layout;
    FontRenderer font;
    layout.init( font );
    int failures = 0;
    // Slab dims mirror what InventoryRenderer::compute_layout produces:
    // the auto-fit stride shrinks so the slab always fits the screen.
    failures += check_strip( layout, "16:9", 1920.0f, 1080.0f, 500.0f, 55.0f, UI_OVERFLOW_NONE );
    failures += check_strip( layout, "21:9", 2560.0f, 1080.0f, 500.0f, 55.0f, UI_OVERFLOW_NONE );
    failures += check_strip( layout, "32:9", 3840.0f, 1080.0f, 500.0f, 55.0f, UI_OVERFLOW_NONE );
    failures += check_strip( layout, "9:16", 600.0f, 1080.0f, 460.0f, 50.0f, UI_OVERFLOW_NONE );
    // A 480x320 window fits the HUD + survival but not the tall screens
    // (every container backdrop tops out above the viewport).
    failures += check_strip( layout, "tiny", 480.0f, 320.0f, 360.0f, 40.0f,
                           UI_OVERFLOW_CHEST | UI_OVERFLOW_CRAFTING2 | UI_OVERFLOW_CRAFTING3 | UI_OVERFLOW_FURNACE );
    // 240x160 fits nothing but the creative screen (its screen-fraction
    // sizing shrinks with the window); Clay repositions rather than
    // centers at degenerate sizes, so only the mask is asserted.
    failures += check_strip( layout, "extreme", 240.0f, 160.0f, 360.0f, 40.0f,
                           UI_OVERFLOW_HUD | UI_OVERFLOW_SURVIVAL | UI_OVERFLOW_CHEST |
                               UI_OVERFLOW_CRAFTING2 | UI_OVERFLOW_CRAFTING3 | UI_OVERFLOW_FURNACE | UI_OVERFLOW_PAUSE,
                           false );

    // Death screen: the menu collapses to Respawn + Exit.
    {
        UiLayoutInput in;
        in.screen_w = 1920.0f;
        in.screen_h = 1080.0f;
        in.hotbar_slab_w = 500.0f;
        in.hotbar_slab_h = 60.0f;
        in.cell_stride = 55.0f;
        in.label_h = 18.0f;
        in.pause_dead = true;
        layout.compute( in );
        const bool ok = layout.rects( ).pause_row_count == 2;
        pr_test( "  %-58s %s", "dead: pause menu has 2 rows", ok ? "PASS" : "FAIL" );
        if ( !ok ) {
            failures++;
        }
    }
    layout.destroy( );
    return failures;
}
