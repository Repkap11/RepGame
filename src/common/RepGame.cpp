// RepGame core: harness wrappers and small state accessors. The bulk of
// the implementation is split by concern into src/common/game/.

#include "common/RepGame.hpp"
#include "common/chest.hpp"
#include "common/constants.hpp"
#include "common/world.hpp"
#include "common/harness/run_policy.hpp"
#include "common/game/game_internal.hpp"

// Capture/fly/walktest harness state and REPGAME_* parsing now live in
// src/common/harness/run_policy.cpp — see RunPolicy there.

bool RepGame::capture_mode( ) {
    return run_policy( ).capture.enabled;
}

bool RepGame::fly_mode( ) {
    return run_policy( ).fly.enabled;
}

void RepGame::mp_test_goto( RepGameState &state, const glm::dvec3 &pos ) {
    // Move the fly origin so a speed-0 pin tracks the command.
    run_policy( ).fly.start = pos;
    state.camera.pos = pos;
    state.camera.prev_pos = pos;
}

bool RepGame::shouldExit( ) const {
    return globalGameState.input.exitGame;
}

bool RepGame::should_lock_pointer( ) const {
    return !run_policy().capture.enabled && !globalGameState.input.inventory_open && !globalGameState.input.menu_open && !globalGameState.input.text_input_active;
}

void RepGame::changeSize( const int w, const int h ) {
    // pr_debug( "Screen Size Change:%dx%d", w, h );
    if ( w == 0 || h == 0 ) {
        return;
    }
    globalGameState.screen.width = static_cast<float>( w );
    globalGameState.screen.height = static_cast<float>( h );
    glViewport( 0, 0, w, h );

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;
    globalGameState.ui.overlay.on_screen_size_change( w, h );
    globalGameState.ui.font.onScreenSizeChange( w, h );
    // Sizing pass: every panel the Clay layout positions gets its grid
    // metrics first — compute_layout is pure (no GL rebuild).
    globalGameState.hotbar.inventory_renderer.compute_layout( w, h );
    globalGameState.survival_inventory.inventory_renderer.compute_layout( w, h );
    globalGameState.ui.creative.inventory_renderer.compute_layout( w, h );
    // Container grids share the survival stride — size them before the
    // pass so the declared slab sizes match.
    repgame_layout_containers( globalGameState );
    globalGameState.ui.layout.compute( ui_layout_input( globalGameState ) );
    // Adopt the measured rects: each slab lands where the layout put it
    // (the hotbar flush at the screen bottom, the survival inventory above
    // the HUD strip, the creative palette centered + floor-lifted, the
    // container screens floating over the survival slab).
    const UiLayoutRects &rects = globalGameState.ui.layout.rects( );
    globalGameState.hotbar.apply_layout( rects.hotbar );
    globalGameState.survival_inventory.apply_layout( rects.survival_items );
    globalGameState.ui.creative.apply_layout( w, h, rects.creative_items, rects.search_box );
    repgame_apply_ui_layout( globalGameState );
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, CAMERA_FAR_PLANE );
    globalGameState.screen.ortho = glm::ortho<float>( 0.f, w, 0.f, h, -1.f, 1.f );
    globalGameState.screen.ortho_center = glm::ortho<float>( -w / 2.0f, w / 2.0f, -h / 2.0f, h / 2.0f, -1.f, 1.f );
    globalGameState.world.change_size( w, h );
}

void RepGame::clear( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}

void RepGame::get_screen_size( int *width, int *height ) const {
    *width = globalGameState.screen.width;
    *height = globalGameState.screen.height;
}

GameMode RepGame::getGameMode( ) const {
    return globalGameState.game_mode;
}

bool RepGame::player_targetable( const RepGameState &state ) {
    return !state.peaceful && !state.input.no_clip && !state.vitals.dead;
}

Input &RepGame::getInputState( ) {
    return globalGameState.input;
}