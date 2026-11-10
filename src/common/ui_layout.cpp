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

// Selected-item label size (game_render draws at this size too).
constexpr float UI_ITEM_LABEL_SIZE = 22.0f;

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
// button cells. `k` is the integer text scale (labels 13k, title
// 13(k+1)); the caller fit-solves it.
static void declare_pause( const UiLayoutInput &in, const FontRenderer &font, const int k ) {
    const float t = 13.0f * k;
    const float title_size = 13.0f * ( k + 1 );
    const float bh = t + 2.0f * lroundf( t * 0.35f );
    const float gap = lroundf( t * 0.4f );
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
                        // A fixed-stride slab is stride*dim + one border.
                        const auto slab = [ s2 ]( float dim ) { return s2 * ( dim + 0.1f ); };
                        // Chest: 9x3 slab, bottom edge a quarter stride
                        // above the survival slab.
                        CLAY( { .id = CLAY_ID( "chest_items" ),
                                .layout = { .sizing = { CLAY_SIZING_FIXED( slab( 9.0f ) ), CLAY_SIZING_FIXED( slab( 3.0f ) ) } },
                                .floating = { .offset = { 0, -0.25f * s2 },
                                              .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                              .attachTo = CLAY_ATTACH_TO_PARENT } } ) { }
                        // Crafting rows: grid | one-stride arrow gap |
                        // output, the output cell vertically centered on
                        // the grid. Bottom edge pinned like the chest's.
                        for ( int dim = 2; dim <= 3; dim++ ) {
                            CLAY( { .id = CLAY_IDI( "crafting_row", dim ),
                                    .layout = { .sizing = { CLAY_SIZING_FIT( ), CLAY_SIZING_FIT( ) },
                                                .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
                                                .layoutDirection = CLAY_LEFT_TO_RIGHT },
                                    .floating = { .offset = { 0, -0.25f * s2 },
                                                  .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                                  .attachTo = CLAY_ATTACH_TO_PARENT } } ) {
                                CLAY( { .id = CLAY_IDI( "crafting_grid", dim ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( slab( ( float )dim ) ), CLAY_SIZING_FIXED( slab( ( float )dim ) ) } } } ) { }
                                CLAY( { .id = CLAY_IDI( "crafting_gap", dim ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( s2 ), CLAY_SIZING_FIXED( slab( 1.0f ) ) } } } ) { }
                                CLAY( { .id = CLAY_IDI( "crafting_out", dim ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( slab( 1.0f ) ), CLAY_SIZING_FIXED( slab( 1.0f ) ) } } } ) { }
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
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( slab( 1.0f ) ), CLAY_SIZING_FIXED( slab( 1.0f ) ) } } } ) { }
                                CLAY( { .id = CLAY_ID( "furnace_mid" ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( 1.8f * s2 ), CLAY_SIZING_FIXED( slab( 1.0f ) ) } } } ) { }
                                CLAY( { .id = CLAY_ID( "furnace_output" ),
                                        .layout = { .sizing = { CLAY_SIZING_FIXED( slab( 1.0f ) ), CLAY_SIZING_FIXED( slab( 1.0f ) ) } } } ) { }
                            }
                            CLAY( { .id = CLAY_ID( "furnace_vgap" ), .layout = { .sizing = { CLAY_SIZING_FIXED( 0 ), CLAY_SIZING_FIXED( 0.2f * s2 ) } } } ) { }
                            CLAY( { .id = CLAY_ID( "furnace_fuel" ),
                                    .layout = { .sizing = { CLAY_SIZING_FIXED( slab( 1.0f ) ), CLAY_SIZING_FIXED( slab( 1.0f ) ) } } } ) { }
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
        // panel's bottom edge; the search bar floats above its top edge.
        if ( in.creative_panel_w > 0.0f ) {
            CLAY( { .id = CLAY_ID( "creative_panel" ),
                    .layout = { .sizing = { CLAY_SIZING_FIXED( in.creative_panel_w ), CLAY_SIZING_FIXED( in.creative_panel_h ) },
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM } },
                    .floating = { .offset = { 0, -creative_lift },
                                  .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
                                  .attachTo = CLAY_ATTACH_TO_ROOT } } ) {
                CLAY( { .id = CLAY_ID( "creative_items" ),
                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.creative_slab_w ), CLAY_SIZING_FIXED( in.creative_slab_h ) } } } ) { }
                CLAY( { .id = CLAY_ID( "search_box" ),
                        .layout = { .sizing = { CLAY_SIZING_FIXED( in.creative_panel_w ), CLAY_SIZING_FIXED( search_h ) } },
                        .floating = { .offset = { 0, -search_gap },
                                      .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_TOP },
                                      .attachTo = CLAY_ATTACH_TO_PARENT } } ) { }
            }
        }
        // The pause panel floats (attachTo ROOT): it never displaces the
        // HUD strip, and is laid out whether or not the menu is open.
        declare_pause( in, *this->font_, pause_k );
        }
        Clay_EndLayout( );

        const Clay_ElementData panel = Clay_GetElementData( CLAY_ID( "pause_panel" ) );
        const bool fits = !panel.found || ( panel.boundingBox.width <= in.screen_w * 0.95f && panel.boundingBox.height <= in.screen_h * 0.9f );
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

    this->rects_.pause_panel = rect_of( CLAY_ID( "pause_panel" ) );
    this->rects_.pause_title = rect_of( CLAY_ID( "pause_title" ) );
    this->rects_.pause_k = pause_k;
    this->rects_.pause_gap = lroundf( 13.0f * pause_k * 0.4f );
    PauseMenu::Line lines[ PauseMenu::MAX_ROWS ];
    const int nl = PauseMenu::build_lines( in.pause_dead, lines, PauseMenu::MAX_ROWS );
    this->rects_.pause_row_count = nl;
    for ( int i = 0; i < nl; i++ ) {
        this->rects_.pause_rows[ i ] = rect_of( CLAY_IDI( "pause_row", i ) );
    }
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
