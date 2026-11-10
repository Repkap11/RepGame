// The single screen-space UI layout pass. Declares the whole UI as a Clay
// tree, runs one layout, and hands out named rects in RepGame's centered
// y-up coordinates (the conversion lives here — draw code never sees
// Clay's top-left y-down space).
//
// Element sizes are declared in physical pixels; positions land wherever
// the metrics put them (the renderer snaps what needs snapping).

#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/ui_layout.hpp"
#include "common/font_renderer.hpp"
#include "common/inventory_renderer.hpp"
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

void UiLayout::compute( const UiLayoutInput &in ) {
    this->rects_ = { };
    if ( this->context == nullptr || in.screen_w <= 0.0f || in.screen_h <= 0.0f || in.cell_stride <= 0.0f ) {
        return;
    }
    Clay_SetCurrentContext( this->context );
    Clay_SetLayoutDimensions( { in.screen_w, in.screen_h } );
    Clay_BeginLayout( );

    const float s = in.cell_stride;
    CLAY( { .id = CLAY_ID( "root" ),
            .layout = { .sizing = { CLAY_SIZING_GROW( ), CLAY_SIZING_GROW( ) },
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_BOTTOM },
                        .layoutDirection = CLAY_TOP_TO_BOTTOM } } ) {
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
    }
    Clay_EndLayout( );

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
    return in;
}
