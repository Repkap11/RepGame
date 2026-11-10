// The Clay-backed UI layout pass: HUD-strip rect geometry across aspect
// ratios. Runs without GL — the FontRenderer here is only bound as the
// (never-invoked) MeasureText callback; no TEXT elements are declared.

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
    layout.destroy( );
    return failures;
}
