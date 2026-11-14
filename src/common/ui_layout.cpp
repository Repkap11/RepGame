// The single screen-space UI layout pass. Declares the whole UI as a Clay
// tree, runs one layout, and hands out named rects in RepGame's centered
// y-up coordinates (the conversion lives here — draw code never sees
// Clay's top-left y-down space).
//
// Element sizes are declared in physical pixels; positions land wherever
// the metrics put them (the renderer snaps what needs snapping).

#include <math.h>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/ui_layout.hpp"
#include "common/font_renderer.hpp"
#include "common/inventory_renderer.hpp"
#include "common/pause_menu.hpp"
#include "common/vendor/clay.h"

// Inter-row/cell gap at pause text scale k — declared in the rows
// container and re-read for rects_.pause_gap (split_row's cell gap).
static float pause_gap_for( const int k ) {
    return lroundf( UI_TEXT_BASE_SIZE * k * 0.4f );
}

static void clay_error( Clay_ErrorData data ) {
    pr_debug( "clay layout error %d: %.*s", data.errorType, ( int )data.errorText.length, data.errorText.chars );
}

static Clay_Dimensions clay_measure_text( Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data ) {
    const FontRenderer &font = *static_cast<const FontRenderer *>( user_data );
    float w = 0, h = 0;
    font.measureSlice( text.chars, text.length, static_cast<float>( config->fontSize ), w, h );
    return { w, h };
}

void UiLayout::init( FontRenderer &font ) {
    const uint32_t bytes = Clay_MinMemorySize( );
    this->arena_mem = malloc( bytes );
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory( bytes, this->arena_mem );
    this->context = Clay_Initialize( arena, { 0, 0 }, { clay_error, nullptr } );
    this->font_ = &font;
    Clay_SetMeasureTextFunction( clay_measure_text, &font );
}

void UiLayout::destroy( ) {
    // The context lives inside the freed arena — drop Clay's global
    // pointer to it first (Clay_MinMemorySize reads the current context).
    Clay_SetCurrentContext( nullptr );
    free( this->arena_mem );
    this->arena_mem = nullptr;
    this->context = nullptr;
    this->rects_ = { };
}

// Clay box (top-left origin, y-down, px) -> centered-origin y-up UiRect.
static UiRect clay_to_ui( const Clay_BoundingBox &b, float screen_w, float screen_h ) {
    UiRect r;
    r.left = b.x - screen_w * 0.5f;
    r.bottom = screen_h * 0.5f - b.y - b.height;
    r.width = b.width;
    r.height = b.height;
    return r;
}

// The pause menu: a floating centered panel — a cap-height title band
// over one full-width row per button line. Clay owns the stacking and
// fit; PauseMenu::split_row subdivides the returned row rects into equal
// button cells. `k` is the integer text scale (labels k*base, title
// (k+1)*base); the caller fit-solves it.
static void declare_pause( const UiLayoutInput &in, const FontRenderer &font, const int k ) {
    const float t = UI_TEXT_BASE_SIZE * k;
    const float title_size = UI_TEXT_BASE_SIZE * ( k + 1 );
    const float bh = t + 2.0f * lroundf( t * 0.35f );
    const float gap = pause_gap_for( k );
    const float pad = t;  // outer padding on all four sides
    const float hpad = t; // text padding inside a button

    PauseMenu::Line lines[ PauseMenu::MAX_ROWS ];
    const int nl = PauseMenu::build_lines( in.pause_dead, lines, PauseMenu::MAX_ROWS );

    // Content-driven panel width: the widest line measured on worst-case
    // labels, so live text can never overflow its button.
    char worst[ 64 ];
    float panel_inner = 0.0f;
    for ( int i = 0; i < nl; i++ ) {
        const PauseMenu::Line &line = lines[ i ];
        float line_w;
        if ( line.count == 1 && PauseMenu::is_stepper_row( line.kinds[ 0 ] ) ) {
            // - and + squares at the ends, reset button between them.
            const char *label = PauseMenu::worst_label( line.kinds[ 0 ], in.pause_dead, worst, sizeof( worst ) );
            line_w = 2.0f * bh + 2.0f * gap + 2.0f * hpad + font.getTextWidth( label, t );
        } else {
            // Equal cells sharing the line's gap; a single-button line
            // degenerates to its own worst-label width.
            float max_cell = 0.0f;
            for ( int j = 0; j < line.count; j++ ) {
                const char *label = PauseMenu::worst_label( line.kinds[ j ], in.pause_dead, worst, sizeof( worst ) );
                const float cell = font.getTextWidth( label, t ) + 2.0f * hpad;
                if ( cell > max_cell ) {
                    max_cell = cell;
                }
            }
            line_w = line.count * max_cell + ( line.count - 1 ) * gap;
        }
        if ( line_w > panel_inner ) {
            panel_inner = line_w;
        }
    }

    const uint16_t pad_u = static_cast<uint16_t>( pad );
    const uint16_t gap_u = static_cast<uint16_t>( gap );
    CLAY( { .id = CLAY_ID( "pause_panel" ),
            .layout = { .sizing = { CLAY_SIZING_FIXED( panel_inner + 2.0f * pad ), CLAY_SIZING_FIT( ) },
                        .padding = CLAY_PADDING_ALL( pad_u ),
                        .childGap = pad_u,
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
            // Floating: the modal panel never displaces the HUD strip.
            .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
                          .attachTo = CLAY_ATTACH_TO_ROOT } } ) {
        // Cap-height band; its bottom edge is the title baseline.
        CLAY( { .id = CLAY_ID( "pause_title" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( font.getCapHeight( title_size ) ) } } } ) { }
        CLAY( { .layout = { .sizing = { CLAY_SIZING_GROW( ), CLAY_SIZING_FIT( ) },
                           .childGap = gap_u,
                           .layoutDirection = CLAY_TOP_TO_BOTTOM } } ) {
            for ( int i = 0; i < nl; i++ ) {
                CLAY( { .id = CLAY_IDI( "pause_row", i ),
                        .layout = { .sizing = { CLAY_SIZING_GROW( ), CLAY_SIZING_FIXED( bh ) } } } ) { }
            }
        }
    }
}

// The sign editor: a floating centered panel — a cap-height title band,
// four fixed-height line fields, then a hint band. Always laid out like
// the pause panel; only drawn while the editor is open. `k` shares the
// pause menu's fit-solved text scale.
static void declare_sign( const UiLayoutInput &in, const FontRenderer &font, const int k ) {
    const float t = UI_TEXT_BASE_SIZE * k;
    const float pad = t;
    const float row_h = 1.6f * t;
    const float gap = 0.5f * t;
    const float hint_t = UI_TEXT_BASE_SIZE;
    // Inner width fits a full SIGN_LINE_MAX line plus a cursor's width.
    const float inner_w = font.getTextWidth( "WWWWWWWWWWWWWWWW", t );
    const uint16_t pad_u = static_cast<uint16_t>( lroundf( pad ) );
    const uint16_t gap_u = static_cast<uint16_t>( lroundf( gap ) );
    CLAY( { .id = CLAY_ID( "sign_panel" ),
            .layout = { .sizing = { CLAY_SIZING_FIXED( inner_w + 2.0f * pad ), CLAY_SIZING_FIT( ) },
                        .padding = CLAY_PADDING_ALL( pad_u ),
                        .childGap = gap_u,
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
            // Floating: the modal panel never displaces the HUD strip.
            .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
                          .attachTo = CLAY_ATTACH_TO_ROOT } } ) {
        CLAY( { .id = CLAY_ID( "sign_title" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( font.getCapHeight( t ) ) } } } ) { }
        for ( int i = 0; i < SIGN_LINES; i++ ) {
            CLAY( { .id = CLAY_IDI( "sign_line", i ),
                    .layout = { .sizing = { CLAY_SIZING_GROW( ), CLAY_SIZING_FIXED( row_h ) } } } ) { }
        }
        CLAY( { .id = CLAY_ID( "sign_hint" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( font.getCapHeight( hint_t ) ) } } } ) { }
    }
}

void UiLayout::compute( const UiLayoutInput &in ) {
    this->rects_ = { };
    if ( this->context == nullptr || in.screen_w <= 0.0f || in.screen_h <= 0.0f || in.cell_stride <= 0.0f ) {
        return;
    }
    Clay_SetCurrentContext( this->context );
    Clay_SetLayoutDimensions( { in.screen_w, in.screen_h } );

    const float s = in.cell_stride;
    // The HUD strip is all-FIXED children, so its height is known before
    // the pass — the survival slab's pin and the creative panel's floor
    // lift can be solved here instead of read back. Distances and the
    // centered-coords edges below are pixel values either way; only the
    // sign convention differs.
    const float strip_h = 1.1f * s + in.hotbar_slab_h + in.label_h + 4.0f;
    const float strip_top = -in.screen_h * 0.5f + strip_h; // centered coords
    // Survival slab bottom: the higher of the creative panel's bottom or
    // the HUD strip's top (the old surv_pin rule, expressed as dead space
    // under the inventory group).
    const float creative_bottom = in.creative_panel_h > 0.0f ? fmaxf( -in.creative_panel_h * 0.5f, in.creative_floor ) : -1.0e30f;
    const float pin_gap = fmaxf( 0.0f, creative_bottom - strip_top );
    // The creative panel is screen-centered, lifted off its bottom floor.
    const float creative_lift = fmaxf( 0.0f, in.creative_floor + in.creative_panel_h * 0.5f );
    const float search_h = 0.55f * in.creative_stride;
    const float search_gap = 0.15f * in.creative_stride;

    // Chat console metrics: an integer text scale off the height basis
    // (clamped 1..4), left-anchored above the HUD strip. The scrollback is
    // a stack of fixed-height rows capped at 20 and a third of the screen
    // (kept short of the top-left debug overlay); the input bar is a
    // padded text row pinned under it. Portrait takes
    // the full width; landscape the wider of 40% or 40 glyphs.
    int chat_k = static_cast<int>( lroundf( ui_layout_height_basis( in.screen_w, in.screen_h ) / 720.0f ) );
    if ( chat_k < 1 ) {
        chat_k = 1;
    } else if ( chat_k > 4 ) {
        chat_k = 4;
    }
    const float chat_t = UI_TEXT_BASE_SIZE * chat_k;
    const float chat_line_h = chat_t * 1.25f;
    const float chat_pad = 8.0f;
    const float chat_input_h = chat_line_h * 1.4f;
    int chat_lines = static_cast<int>( fminf( 20.0f, floorf( 0.33f * in.screen_h / chat_line_h ) ) );
    if ( chat_lines < 1 ) {
        chat_lines = 1;
    }
    const float chat_w = fminf( in.screen_w - 2.0f * chat_pad,
                                in.screen_w < in.screen_h ? in.screen_w - 2.0f * chat_pad
                                                          : fmaxf( 0.4f * in.screen_w,
                                                                   this->font_->getTextWidth( "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", chat_t ) ) );

    // Touch controls: button unit r = min(w/4, h/2)/2 (UIOverlayView's
    // lookMaxRadius), buttons 4:3 (r wide, bh tall). Anchored to fixed
    // screen positions — the joystick is a 2r square bottom-left,
    // Break+Place a row bottom-right, Jump/Sneak/Menu on the top
    // corners/edge with Chat hanging under Sneak on the left and Inv
    // mirroring under Jump on the right, Pick right of Menu. On
    // narrow screens the bottom set may cover the hotbar; no lift
    // fallback. The inventory set mirrors Inv/Jump with the creative
    // page arrows floating in the margins.
    const float touch_r = fminf( in.screen_w * 0.25f, in.screen_h * 0.5f ) * 0.5f;
    const float touch_bh = touch_r * 0.75f;
    // Spacing between neighbouring buttons; edge-anchored sides stay
    // flush to the screen edge.
    const float touch_gap = touch_r * 0.04f;
    const float touch_margin = fmaxf( 0.0f, ( in.screen_w - in.creative_panel_w ) * 0.5f );
    const float touch_page_s = fmaxf( 0.0f, fminf( in.screen_w * 0.09f, touch_margin - 8.0f ) );
    const float touch_page_off = fmaxf( 0.0f, ( touch_margin - touch_page_s ) * 0.5f );
    // Page arrows: vertically centered on the creative panel when that
    // already clears Close (Close's bottom edge = h/2 - 2*bh - gap, it
    // hangs under Mode on the right edge); otherwise pushed flush below
    // it. Both arrows share the y so the pair stays level.
    const float touch_close_bottom = in.screen_h * 0.5f - 2.0f * touch_bh - touch_gap;
    const float creative_center_y = creative_bottom + in.creative_panel_h * 0.5f;
    const float touch_page_top = fminf( creative_center_y + touch_page_s * 0.5f, touch_close_bottom );
    const float touch_page_center_off = -( touch_page_top - touch_page_s * 0.5f ); // y-down offset from screen center

    // Fit-solve the pause menu's integer text scale k: shrink it until the
    // panel fits the viewport caps (same rule the old k-- loop applied).
    int pause_k = static_cast<int>( lroundf( ui_layout_height_basis( in.screen_w, in.screen_h ) / 400.0f ) );
    if ( pause_k < 1 ) {
        pause_k = 1;
    }
    for ( ; ; pause_k-- ) {
        Clay_BeginLayout( );

        CLAY( { .id = CLAY_ID( "root" ),
            .layout = { .sizing = { CLAY_SIZING_GROW( ), CLAY_SIZING_GROW( ) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM },
                        .layoutDirection = CLAY_TOP_TO_BOTTOM } } ) {
        // The inventory group: container screens stack above the survival
        // slab here (added with those migrations); pin_gap keeps the whole
        // group clear of the HUD strip when it would otherwise overlap.
        if ( in.surv_slab_w > 0.0f ) {
            CLAY( { .id = CLAY_ID( "inventory_group" ),
                    .layout = { .sizing = { CLAY_SIZING_FIT( ), CLAY_SIZING_FIT( ) },
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                                .layoutDirection = CLAY_TOP_TO_BOTTOM } } ) {
                CLAY( { .id = CLAY_ID( "survival_items" ),
                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.surv_slab_w ), CLAY_SIZING_FIXED( in.surv_slab_h ) } } } ) {
                    // The container screens float off the survival slab's
                    // top edge at the survival grid's stride — every screen
                    // is declared unconditionally so its rect is always
                    // fresh (only the open one draws).
                    const float s2 = in.surv_stride;
                    if ( s2 > 0.0f ) {
                        // Slab sizes are the panels' measured items dims —
                        // the renderer owns the stride+border math.
                        // Chest: 9x3 slab, bottom edge a quarter stride
                        // above the survival slab.
                        CLAY( { .id = CLAY_ID( "chest_items" ),
                                .layout = { .sizing = { CLAY_SIZING_FIXED( in.chest_slab_w ), CLAY_SIZING_FIXED( in.chest_slab_h ) } },
                                .floating = { .offset = { 0, -0.25f * s2 },
                                              .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                              .attachTo = CLAY_ATTACH_TO_PARENT } } ) { }
                        // Crafting rows: grid | one-stride arrow gap |
                        // output, the output cell vertically centered on
                        // the grid. Bottom edge pinned like the chest's.
                        for ( int dim = 2; dim <= 3; dim++ ) {
                            const float grid_slab = dim == 2 ? in.craft_grid2_slab : in.craft_grid3_slab;
                            CLAY( { .id = CLAY_IDI( "crafting_row", dim ),
                                    .layout = { .sizing = { CLAY_SIZING_FIT( ), CLAY_SIZING_FIT( ) },
                                                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                                .layoutDirection = CLAY_LEFT_TO_RIGHT },
                                    .floating = { .offset = { 0, -0.25f * s2 },
                                                  .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                                  .attachTo = CLAY_ATTACH_TO_PARENT } } ) {
                                CLAY( { .id = CLAY_IDI( "crafting_grid", dim ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( grid_slab ), CLAY_SIZING_FIXED( grid_slab ) } } } ) { }
                                CLAY( { .id = CLAY_IDI( "crafting_gap", dim ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( s2 ), CLAY_SIZING_FIXED( in.craft_out_slab ) } } } ) { }
                                CLAY( { .id = CLAY_IDI( "crafting_out", dim ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.craft_out_slab ), CLAY_SIZING_FIXED( in.craft_out_slab ) } } } ) { }
                            }
                        }
                        // Furnace: input | mid gap | output on the top
                        // row, fuel centered below with a 0.2-stride gap;
                        // the fuel cell's bottom pins at 1.15 strides.
                        CLAY( { .id = CLAY_ID( "furnace_group" ),
                                .layout = { .sizing = { CLAY_SIZING_FIT( ), CLAY_SIZING_FIT( ) },
                                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                                            .layoutDirection = CLAY_TOP_TO_BOTTOM },
                                .floating = { .offset = { 0, -1.15f * s2 },
                                              .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                              .attachTo = CLAY_ATTACH_TO_PARENT } } ) {
                            CLAY( { .id = CLAY_ID( "furnace_top" ),
                                    .layout = { .sizing = { CLAY_SIZING_FIT( ), CLAY_SIZING_FIT( ) },
                                                .layoutDirection = CLAY_LEFT_TO_RIGHT } } ) {
                                CLAY( { .id = CLAY_ID( "furnace_input" ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.furnace_slab ), CLAY_SIZING_FIXED( in.furnace_slab ) } } } ) { }
                                CLAY( { .id = CLAY_ID( "furnace_mid" ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( 1.8f * s2 ), CLAY_SIZING_FIXED( in.furnace_slab ) } } } ) { }
                                CLAY( { .id = CLAY_ID( "furnace_output" ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.furnace_slab ), CLAY_SIZING_FIXED( in.furnace_slab ) } } } ) { }
                            }
                            CLAY( { .id = CLAY_ID( "furnace_vgap" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( 0.2f * s2 ) } } } ) { }
                            CLAY( { .id = CLAY_ID( "furnace_fuel" ),
                                    .layout = { .sizing = { CLAY_SIZING_FIXED( in.furnace_slab ), CLAY_SIZING_FIXED( in.furnace_slab ) } } } ) { }
                        }
                    }
                }
            }
            CLAY( { .id = CLAY_ID( "pin_gap" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( pin_gap ) } } } ) { }
        }
        // The bottom HUD strip: label slot over the vitals row over the
        // hotbar slab, plus headroom under the lowest panel. Panel layouts
        // pin to hud_strip.top() — measured, not re-derived.
        CLAY( { .id = CLAY_ID( "hud_strip" ),
                .layout = { .sizing = { CLAY_SIZING_FIT( ), CLAY_SIZING_FIT( ) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER },
                            .layoutDirection = CLAY_TOP_TO_BOTTOM } } ) {
            CLAY( { .id = CLAY_ID( "hud_pad" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( 0.3f * s ) } } } ) { }
            // Reservation slot; its bottom edge is the survival-mode label
            // baseline (0.8*stride + 4 above the hotbar items top).
            CLAY( { .id = CLAY_ID( "item_label" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( in.label_h ) } } } ) { }
            CLAY( { .id = CLAY_ID( "gap_label" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( 0.15f * s + 4.0f ) } } } ) { }
            // Hearts row, same width as the hotbar slab so icon anchoring
            // matches the slab's left/right edges.
            CLAY( { .id = CLAY_ID( "vitals" ), .layout = { .sizing = { CLAY_SIZING_FIXED( in.hotbar_slab_w ), CLAY_SIZING_FIXED( 0.5f * s ) } } } ) { }
            CLAY( { .id = CLAY_ID( "gap_vitals" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( 0.15f * s ) } } } ) { }
            CLAY( { .id = CLAY_ID( "hotbar" ), .layout = { .sizing = { CLAY_SIZING_FIXED( in.hotbar_slab_w ), CLAY_SIZING_FIXED( in.hotbar_slab_h ) } } } ) {
                // Creative mode draws the item name snug above the hotbar
                // (there's no hearts row); this marker's bottom edge is the
                // label baseline there — 0.035*stride + 8 above the slab.
                CLAY( { .id = CLAY_ID( "item_label_low" ),
                        .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( in.label_h ) } },
                        .floating = { .offset = { 0, -( 0.035f * s + 8.0f ) },
                                      .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                      .attachTo = CLAY_ATTACH_TO_PARENT } } ) { }
            }
        }
        // The creative palette: a floating screen-centered panel (the old
        // max_width/max_height bounds), lifted off its bottom floor when
        // the screen is too small to center it. The items slab hugs the
        // panel's bottom edge; the search bar floats above the SLAB's top
        // edge — the panel can be taller than the screen, so anchoring to
        // it would put the bar off-screen at ultrawide aspects.
        if ( in.creative_panel_w > 0.0f ) {
            CLAY( { .id = CLAY_ID( "creative_panel" ),
                    .layout = { .sizing = { CLAY_SIZING_FIXED( in.creative_panel_w ), CLAY_SIZING_FIXED( in.creative_panel_h ) },
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM } },
                    .floating = { .offset = { 0, -creative_lift },
                                  .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
                                  .attachTo = CLAY_ATTACH_TO_ROOT } } ) {
                CLAY( { .id = CLAY_ID( "creative_items" ),
                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.creative_slab_w ), CLAY_SIZING_FIXED( in.creative_slab_h ) } } } ) {
                    CLAY( { .id = CLAY_ID( "search_box" ),
                            .layout = { .sizing = { CLAY_SIZING_FIXED( in.creative_panel_w ), CLAY_SIZING_FIXED( search_h ) } },
                            .floating = { .offset = { 0, -search_gap },
                                          .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                          .attachTo = CLAY_ATTACH_TO_PARENT } } ) { }
                }
            }
        }
        // The chat console floats off the screen's bottom-left corner
        // (attachTo ROOT): left-anchored, the input bar a small pad above
        // the HUD strip's top edge, the scrollback stacked over it. Laid
        // out unconditionally — only the draw pass decides visibility.
        CLAY( { .id = CLAY_ID( "chat_input" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( chat_w ), CLAY_SIZING_FIXED( chat_input_h ) } },
                .floating = { .offset = { chat_pad, -( strip_h + chat_pad ) },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "chat_log" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( chat_w ), CLAY_SIZING_FIXED( chat_lines * chat_line_h ) } },
                .floating = { .offset = { chat_pad, -( strip_h + 2.0f * chat_pad + chat_input_h ) },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // Touch controls float off the root edges at fixed positions;
        // on narrow screens the bottom set may cover the hotbar.
        // Declared unconditionally; only the touch draw decides
        // visibility.
        CLAY( { .id = CLAY_ID( "touch_joystick" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( 2.0f * touch_r ), CLAY_SIZING_FIXED( 2.0f * touch_r ) } },
                .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_jump" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_sneak" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_menu" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_TOP, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // Chat hangs off the left edge under Sneak with a touch_gap
        // between them (mirrors Inv under Jump on the right).
        CLAY( { .id = CLAY_ID( "touch_chat" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .offset = { 0, touch_bh + touch_gap },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // Pick sits top-aligned right of Menu with a touch_gap (menu is
        // r wide, centered on CENTER_TOP, so its right edge is +r/2).
        CLAY( { .id = CLAY_ID( "touch_pick" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .offset = { touch_r * 0.5f + touch_gap, 0 },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // Inv hangs off the right edge under Jump with a touch_gap.
        CLAY( { .id = CLAY_ID( "touch_inventory" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .offset = { 0, touch_bh + touch_gap },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // Break left of Place with a touch_gap; Place stays flush on
        // the right edge.
        CLAY( { .id = CLAY_ID( "touch_break" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .offset = { -( touch_r + touch_gap ), 0 },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_place" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // Inventory-open set: Close at Inv's spot (under Mode with a
        // touch_gap), mode at Jump's, page arrows vertically centered
        // in the margins beside the creative panel (shrunk to fit when
        // the margin is tight).
        CLAY( { .id = CLAY_ID( "touch_inv_close" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .offset = { 0, touch_bh + touch_gap },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_inv_mode" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_r ), CLAY_SIZING_FIXED( touch_bh ) } },
                .floating = { .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_page_prev" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_page_s ), CLAY_SIZING_FIXED( touch_page_s ) } },
                .floating = { .offset = { touch_page_off, touch_page_center_off },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_CENTER, .parent = CLAY_ATTACH_POINT_LEFT_CENTER },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        CLAY( { .id = CLAY_ID( "touch_page_next" ),
                .layout = { .sizing = { CLAY_SIZING_FIXED( touch_page_s ), CLAY_SIZING_FIXED( touch_page_s ) } },
                .floating = { .offset = { -touch_page_off, touch_page_center_off },
                              .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_CENTER, .parent = CLAY_ATTACH_POINT_RIGHT_CENTER },
                              .attachTo = CLAY_ATTACH_TO_ROOT } } ) { }
        // The pause panel floats (attachTo ROOT): it never displaces the
        // HUD strip, and is laid out whether or not the menu is open. The
        // sign editor shares its fit-solved text scale and sits in the
        // same fit check so small windows shrink both.
        declare_pause( in, *this->font_, pause_k );
        declare_sign( in, *this->font_, pause_k );
        }
        Clay_EndLayout( );

        const Clay_ElementData panel = Clay_GetElementData( CLAY_ID( "pause_panel" ) );
        const Clay_ElementData sign_panel = Clay_GetElementData( CLAY_ID( "sign_panel" ) );
        const bool fits = ( !panel.found || ( panel.boundingBox.width <= in.screen_w * 0.95f && panel.boundingBox.height <= in.screen_h * 0.9f ) ) &&
                          ( !sign_panel.found || ( sign_panel.boundingBox.width <= in.screen_w * 0.95f && sign_panel.boundingBox.height <= in.screen_h * 0.9f ) );
        if ( fits || pause_k <= 1 ) {
            break;
        }
    }

    const float w = in.screen_w;
    const float h = in.screen_h;
    const auto rect_of = [ & ]( Clay_ElementId id ) {
        const Clay_ElementData d = Clay_GetElementData( id );
        return d.found ? clay_to_ui( d.boundingBox, w, h ) : UiRect{ };
    };
    this->rects_.hotbar = rect_of( CLAY_ID( "hotbar" ) );
    this->rects_.vitals = rect_of( CLAY_ID( "vitals" ) );
    this->rects_.item_label = rect_of( CLAY_ID( "item_label" ) );
    this->rects_.item_label_low = rect_of( CLAY_ID( "item_label_low" ) );
    this->rects_.hud_strip = rect_of( CLAY_ID( "hud_strip" ) );
    this->rects_.survival_items = rect_of( CLAY_ID( "survival_items" ) );
    this->rects_.creative_panel = rect_of( CLAY_ID( "creative_panel" ) );
    this->rects_.creative_items = rect_of( CLAY_ID( "creative_items" ) );
    this->rects_.search_box = rect_of( CLAY_ID( "search_box" ) );

    this->rects_.chest_items = rect_of( CLAY_ID( "chest_items" ) );
    this->rects_.crafting_grid2 = rect_of( CLAY_IDI( "crafting_grid", 2 ) );
    this->rects_.crafting_out2 = rect_of( CLAY_IDI( "crafting_out", 2 ) );
    this->rects_.crafting_gap2 = rect_of( CLAY_IDI( "crafting_gap", 2 ) );
    this->rects_.crafting_grid3 = rect_of( CLAY_IDI( "crafting_grid", 3 ) );
    this->rects_.crafting_out3 = rect_of( CLAY_IDI( "crafting_out", 3 ) );
    this->rects_.crafting_gap3 = rect_of( CLAY_IDI( "crafting_gap", 3 ) );
    this->rects_.furnace_input = rect_of( CLAY_ID( "furnace_input" ) );
    this->rects_.furnace_mid = rect_of( CLAY_ID( "furnace_mid" ) );
    this->rects_.furnace_output = rect_of( CLAY_ID( "furnace_output" ) );
    this->rects_.furnace_fuel = rect_of( CLAY_ID( "furnace_fuel" ) );

    // Window backdrops: the padded union of the screen's panels and the
    // survival slab — the gap between them clicks as inside-the-window.
    const float bpad = 0.35f * in.surv_stride;
    const auto uni = []( UiRect a, const UiRect &b ) {
        const float l = fminf( a.left, b.left ), r = fmaxf( a.right( ), b.right( ) );
        const float bt = fminf( a.bottom, b.bottom ), t = fmaxf( a.top( ), b.top( ) );
        a.left = l;
        a.width = r - l;
        a.bottom = bt;
        a.height = t - bt;
        return a;
    };
    const auto inflate = [ bpad ]( UiRect r ) {
        r.left -= bpad;
        r.width += 2.0f * bpad;
        r.bottom -= bpad;
        r.height += 2.0f * bpad;
        return r;
    };
    this->rects_.chest_backdrop = inflate( uni( this->rects_.chest_items, this->rects_.survival_items ) );
    this->rects_.crafting_backdrop2 = inflate( uni( uni( this->rects_.crafting_grid2, this->rects_.crafting_out2 ), this->rects_.survival_items ) );
    this->rects_.crafting_backdrop3 = inflate( uni( uni( this->rects_.crafting_grid3, this->rects_.crafting_out3 ), this->rects_.survival_items ) );
    this->rects_.furnace_backdrop = inflate( uni( uni( uni( this->rects_.furnace_input, this->rects_.furnace_output ), this->rects_.furnace_fuel ), this->rects_.survival_items ) );

    this->rects_.sign_panel = rect_of( CLAY_ID( "sign_panel" ) );
    this->rects_.sign_title = rect_of( CLAY_ID( "sign_title" ) );
    this->rects_.sign_hint = rect_of( CLAY_ID( "sign_hint" ) );
    this->rects_.sign_k = pause_k;
    for ( int i = 0; i < SIGN_LINES; i++ ) {
        this->rects_.sign_lines[ i ] = rect_of( CLAY_IDI( "sign_line", i ) );
    }

    this->rects_.touch_joystick = rect_of( CLAY_ID( "touch_joystick" ) );
    this->rects_.touch_jump = rect_of( CLAY_ID( "touch_jump" ) );
    this->rects_.touch_sneak = rect_of( CLAY_ID( "touch_sneak" ) );
    this->rects_.touch_menu = rect_of( CLAY_ID( "touch_menu" ) );
    this->rects_.touch_chat = rect_of( CLAY_ID( "touch_chat" ) );
    this->rects_.touch_inventory = rect_of( CLAY_ID( "touch_inventory" ) );
    this->rects_.touch_break = rect_of( CLAY_ID( "touch_break" ) );
    this->rects_.touch_pick = rect_of( CLAY_ID( "touch_pick" ) );
    this->rects_.touch_place = rect_of( CLAY_ID( "touch_place" ) );
    this->rects_.touch_inv_close = rect_of( CLAY_ID( "touch_inv_close" ) );
    this->rects_.touch_inv_mode = rect_of( CLAY_ID( "touch_inv_mode" ) );
    this->rects_.touch_page_prev = rect_of( CLAY_ID( "touch_page_prev" ) );
    this->rects_.touch_page_next = rect_of( CLAY_ID( "touch_page_next" ) );

    this->rects_.chat_input = rect_of( CLAY_ID( "chat_input" ) );
    this->rects_.chat_log = rect_of( CLAY_ID( "chat_log" ) );
    this->rects_.chat_k = chat_k;
    this->rects_.chat_lines = chat_lines;

    this->rects_.pause_panel = rect_of( CLAY_ID( "pause_panel" ) );
    this->rects_.pause_title = rect_of( CLAY_ID( "pause_title" ) );
    this->rects_.pause_k = pause_k;
    this->rects_.pause_gap = pause_gap_for( pause_k );
    PauseMenu::Line lines[ PauseMenu::MAX_ROWS ];
    const int nl = PauseMenu::build_lines( in.pause_dead, lines, PauseMenu::MAX_ROWS );
    this->rects_.pause_row_count = nl;
    for ( int i = 0; i < nl; i++ ) {
        this->rects_.pause_rows[ i ] = rect_of( CLAY_IDI( "pause_row", i ) );
    }

    this->validate( in, strip_h );
}

void UiLayout::validate( const UiLayoutInput &in, const float strip_h ) {
    UiLayoutRects &r = this->rects_;
    r.overflow = UI_OVERFLOW_NONE;

    // hud_strip is all-FIXED children, so its measured height must equal
    // the strip_h the pin math assumed — a drift between the declared
    // children and the precomputed sum silently reintroduces overlap bugs.
    if ( fabsf( r.hud_strip.height - strip_h ) > 0.5f ) {
        pr_debug( "ui_layout: hud_strip height %.2f differs from the assumed %.2f", r.hud_strip.height, strip_h );
    }

    const float eps = 0.5f;
    const auto finite = []( const UiRect &q ) {
        return isfinite( q.left ) && isfinite( q.bottom ) && isfinite( q.width ) && isfinite( q.height );
    };
    const auto overflows = [ & ]( const UiRect &q ) {
        return q.left < -in.screen_w * 0.5f - eps || q.right( ) > in.screen_w * 0.5f + eps ||
               q.bottom < -in.screen_h * 0.5f - eps || q.top( ) > in.screen_h * 0.5f + eps;
    };
    const auto screen = [ & ]( unsigned bit, const char *name, const UiRect &q ) {
        if ( !finite( q ) ) {
            pr_debug( "ui_layout: %s is non-finite [%g,%g %gx%g]", name, q.left, q.bottom, q.width, q.height );
            r.overflow |= bit;
            return;
        }
        if ( overflows( q ) ) {
            r.overflow |= bit;
            if ( !( this->last_overflow_ & bit ) ) {
                pr_debug( "ui_layout: %s overflows %.0fx%.0f viewport: [%.0f,%.0f %.0fx%.0f]", name, in.screen_w, in.screen_h, q.left, q.bottom, q.width, q.height );
            }
        }
    };
    screen( UI_OVERFLOW_HUD, "hud_strip", r.hud_strip );
    screen( UI_OVERFLOW_SURVIVAL, "survival_items", r.survival_items );
    // The creative screen's drawn parts: items slab + floating search bar
    // (the panel is undrawn bounds and may legitimately be screen-tall).
    screen( UI_OVERFLOW_CREATIVE, "creative_items", r.creative_items );
    screen( UI_OVERFLOW_CREATIVE, "search_box", r.search_box );
    screen( UI_OVERFLOW_CHEST, "chest", r.chest_backdrop );
    screen( UI_OVERFLOW_CRAFTING2, "crafting2", r.crafting_backdrop2 );
    screen( UI_OVERFLOW_CRAFTING3, "crafting3", r.crafting_backdrop3 );
    screen( UI_OVERFLOW_FURNACE, "furnace", r.furnace_backdrop );
    screen( UI_OVERFLOW_PAUSE, "pause_panel", r.pause_panel );
    screen( UI_OVERFLOW_SIGN, "sign_panel", r.sign_panel );
    screen( UI_OVERFLOW_CHAT, "chat_input", r.chat_input );
    screen( UI_OVERFLOW_CHAT, "chat_log", r.chat_log );
    screen( UI_OVERFLOW_TOUCH, "touch_joystick", r.touch_joystick );
    screen( UI_OVERFLOW_TOUCH, "touch_jump", r.touch_jump );
    screen( UI_OVERFLOW_TOUCH, "touch_sneak", r.touch_sneak );
    screen( UI_OVERFLOW_TOUCH, "touch_menu", r.touch_menu );
    screen( UI_OVERFLOW_TOUCH, "touch_chat", r.touch_chat );
    screen( UI_OVERFLOW_TOUCH, "touch_inventory", r.touch_inventory );
    screen( UI_OVERFLOW_TOUCH, "touch_break", r.touch_break );
    screen( UI_OVERFLOW_TOUCH, "touch_pick", r.touch_pick );
    screen( UI_OVERFLOW_TOUCH, "touch_place", r.touch_place );
    screen( UI_OVERFLOW_TOUCH, "touch_inv_close", r.touch_inv_close );
    screen( UI_OVERFLOW_TOUCH, "touch_inv_mode", r.touch_inv_mode );
    screen( UI_OVERFLOW_TOUCH, "touch_page_prev", r.touch_page_prev );
    screen( UI_OVERFLOW_TOUCH, "touch_page_next", r.touch_page_next );

    // Sibling non-overlap invariants — a violation here is a real layout
    // bug (not a fit problem). Each pair gets a bit so warnings fire once
    // per newly-overlapping pair, not per resize event.
    uint64_t overlap_mask = 0;
    const auto overlap = [ & ]( uint64_t bit, const char *name, const UiRect &a, const UiRect &b ) {
        if ( !( finite( a ) && finite( b ) &&
                a.left < b.right( ) - eps && a.right( ) > b.left + eps &&
                a.bottom < b.top( ) - eps && a.top( ) > b.bottom + eps ) ) {
            return;
        }
        overlap_mask |= bit;
        if ( !( this->last_overlap_ & bit ) ) {
            pr_debug( "ui_layout: %s overlaps [%g,%g %gx%g] vs [%g,%g %gx%g]", name,
                      a.left, a.bottom, a.width, a.height, b.left, b.bottom, b.width, b.height );
        }
    };
    // Container screens stack above the survival slab — never inside it.
    overlap( 1ull << 0, "chest_items/survival_items", r.chest_items, r.survival_items );
    overlap( 1ull << 1, "crafting_grid2/survival_items", r.crafting_grid2, r.survival_items );
    overlap( 1ull << 2, "crafting_grid3/survival_items", r.crafting_grid3, r.survival_items );
    overlap( 1ull << 3, "furnace_input/survival_items", r.furnace_input, r.survival_items );
    overlap( 1ull << 4, "furnace_fuel/survival_items", r.furnace_fuel, r.survival_items );
    // Crafting row: grid | gap | output are disjoint.
    overlap( 1ull << 5, "crafting_grid2/gap", r.crafting_grid2, r.crafting_gap2 );
    overlap( 1ull << 6, "crafting_gap2/out", r.crafting_gap2, r.crafting_out2 );
    overlap( 1ull << 7, "crafting_grid3/gap", r.crafting_grid3, r.crafting_gap3 );
    overlap( 1ull << 8, "crafting_gap3/out", r.crafting_gap3, r.crafting_out3 );
    // Furnace trio are disjoint.
    overlap( 1ull << 9, "furnace_input/fuel", r.furnace_input, r.furnace_fuel );
    overlap( 1ull << 10, "furnace_output/fuel", r.furnace_output, r.furnace_fuel );
    overlap( 1ull << 11, "furnace_input/output", r.furnace_input, r.furnace_output );
    // Search bar floats above the panel's items slab, never inside it.
    overlap( 1ull << 12, "search_box/creative_items", r.search_box, r.creative_items );
    // Pause rows are disjoint. Each adjacent pair takes a bit past the
    // named pairs — max shift is 13 + (MAX_ROWS - 2) ≤ 63.
    static_assert( UI_LAYOUT_MAX_ROWS <= 52, "pause-row overlap bits exceed the mask" );
    for ( int i = 0; i + 1 < r.pause_row_count; i++ ) {
        overlap( 1ull << ( 13 + i ), "pause_row/next", r.pause_rows[ i ], r.pause_rows[ i + 1 ] );
    }
    // Sign editor line fields are disjoint rows — bits 45+ sit above the
    // pause-row range (13 + MAX_ROWS-2 = 35 max).
    for ( int i = 0; i + 1 < SIGN_LINES; i++ ) {
        overlap( 1ull << ( 45 + i ), "sign_line/next", r.sign_lines[ i ], r.sign_lines[ i + 1 ] );
    }
    // The chat column sits above the HUD strip and the log above the
    // input bar — never inside either.
    overlap( 1ull << 48, "chat_input/hud_strip", r.chat_input, r.hud_strip );
    overlap( 1ull << 49, "chat_log/chat_input", r.chat_log, r.chat_input );
    this->last_overlap_ = overlap_mask;
    this->last_overflow_ = r.overflow;
}

UiLayoutInput ui_layout_input( const RepGameState &state ) {
    UiLayoutInput in;
    in.screen_w = state.screen.width;
    in.screen_h = state.screen.height;
    const InventoryRenderer &hotbar = state.hotbar.inventory_renderer;
    in.hotbar_slab_w = static_cast<float>( hotbar.getItemsRight( ) - hotbar.getItemsLeft( ) );
    in.hotbar_slab_h = static_cast<float>( hotbar.getItemsTop( ) - hotbar.getItemsBottom( ) );
    in.cell_stride = hotbar.getCellStride( );
    in.label_h = state.ui.font.getCapHeight( UI_ITEM_LABEL_SIZE );
    const InventoryRenderer &surv = state.survival_inventory.inventory_renderer;
    in.surv_slab_w = static_cast<float>( surv.getItemsRight( ) - surv.getItemsLeft( ) );
    in.surv_slab_h = static_cast<float>( surv.getItemsTop( ) - surv.getItemsBottom( ) );
    in.surv_stride = surv.getCellStride( );
    // Container slab sizes come off each panel's measured items rect — the
    // renderer owns the stride+border math (the layout declares exactly
    // what the panel will draw).
    const auto slab_w = []( const InventoryRenderer &r ) {
        return static_cast<float>( r.getItemsRight( ) - r.getItemsLeft( ) );
    };
    const InventoryRenderer &chest = state.ui.chest.inventory_renderer;
    in.chest_slab_w = slab_w( chest );
    in.chest_slab_h = static_cast<float>( chest.getItemsTop( ) - chest.getItemsBottom( ) );
    in.craft_grid2_slab = slab_w( state.ui.crafting.grid2 );
    in.craft_grid3_slab = slab_w( state.ui.crafting.grid3 );
    in.craft_out_slab = slab_w( state.ui.crafting.output );
    in.furnace_slab = slab_w( state.ui.furnace.input );
    const InventoryRenderer &creative = state.ui.creative.inventory_renderer;
    in.creative_slab_w = static_cast<float>( creative.getItemsRight( ) - creative.getItemsLeft( ) );
    in.creative_slab_h = static_cast<float>( creative.getItemsTop( ) - creative.getItemsBottom( ) );
    in.creative_panel_w = static_cast<float>( creative.getInvRight( ) - creative.getInvLeft( ) );
    in.creative_panel_h = static_cast<float>( creative.getInvTop( ) - creative.getInvBottom( ) );
    in.creative_stride = creative.getCellStride( );
    // The panel's bottom floor: the reserved strip above the screen bottom
    // (one cell border of breathing room, mirroring compute_layout).
    in.creative_floor = -in.screen_h * 0.5f + static_cast<int>( ui_layout_height_basis( in.screen_w, in.screen_h ) * creative.options.bottom_reserved_percent ) + creative.getCellOffset( );
    in.pause_dead = state.ui.pause.is_dead( );
    return in;
}
