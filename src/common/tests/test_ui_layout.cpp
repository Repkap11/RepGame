// The Clay-backed UI layout pass: HUD-strip + pause-menu rect geometry
// across aspect ratios. Runs without GL — the FontRenderer here is
// uninit'd, so text measures as zero (deterministic) and only the
// panel/row structure is asserted, not label widths.

#include "test_util.hpp"
#include "common/font_renderer.hpp"
#include "common/ui_layout.hpp"

static int check_strip( UiLayout &layout, const char *name, float w, float h, float slab_w, float stride ) {
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
    // sized with a 0.1*basis bottom reservation.
    in.surv_slab_w = 9.1f * stride;
    in.surv_slab_h = 3.1f * stride;
    in.creative_slab_w = fminf( w * 0.7f, 720.0f );
    in.creative_slab_h = 6.1f * 60.0f;
    in.creative_panel_w = w * 0.75f;
    const float basis = fmaxf( h, w * ( 9.0f / 16.0f ) );
    in.creative_panel_h = fminf( 0.75f * basis, h );
    in.creative_stride = 60.0f;
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
    snprintf( what, sizeof( what ), "%s: search bar over panel top + gap", name );
    near_to( what, r.search_box.bottom, r.creative_panel.top( ) + 0.15f * in.creative_stride );
    snprintf( what, sizeof( what ), "%s: search bar sized by stride", name );
    near_to( what, r.search_box.height, 0.55f * in.creative_stride );
    snprintf( what, sizeof( what ), "%s: search bar spans the panel", name );
    near_to( what, r.search_box.left, r.creative_panel.left );
    near_to( what, r.search_box.width, r.creative_panel.width );

    // Pause menu: floating centered panel of stacked rows.
    snprintf( what, sizeof( what ), "%s: pause panel centered", name );
    near_to( what, r.pause_panel.center_x( ), 0.0f );
    snprintf( what, sizeof( what ), "%s: pause panel vertically centered", name );
    near_to( what, r.pause_panel.center_y( ), 0.0f );
    snprintf( what, sizeof( what ), "%s: pause text scale sane", name );
    is_true( what, r.pause_k >= 1 );
    snprintf( what, sizeof( what ), "%s: pause rows laid out", name );
    is_true( what, r.pause_row_count > 0 && r.pause_row_count <= UiLayoutRects::UI_LAYOUT_MAX_ROWS );
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
    failures += check_strip( layout, "16:9", 1920.0f, 1080.0f, 500.0f, 55.0f );
    failures += check_strip( layout, "21:9", 2560.0f, 1080.0f, 500.0f, 55.0f );
    failures += check_strip( layout, "32:9", 3840.0f, 1080.0f, 500.0f, 55.0f );
    failures += check_strip( layout, "9:16", 600.0f, 1080.0f, 460.0f, 50.0f );
    failures += check_strip( layout, "tiny", 480.0f, 320.0f, 360.0f, 40.0f );

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
