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

// The full resize chain on a state: screen dims + GL viewport, mouse
// recentering, inventory grid metrics, the Clay layout pass, slab
// adoption, and the projection matrices. RepGame::changeSize delegates
// here; the rep-dbg `resize` verb calls it directly to verify layout at
// arbitrary sizes (the platform window keeps its real size — under a
// headless/offscreen driver the GL framebuffer clips, so only layout
// output like `uistate` is faithful at sizes other than the real one).
void repgame_set_screen_size( RepGameState &state, const int w, const int h ) {
    if ( w == 0 || h == 0 ) {
        return;
    }
    state.screen.width = static_cast<float>( w );
    state.screen.height = static_cast<float>( h );
    glViewport( 0, 0, w, h );

    state.input.mouse.currentPosition.x = w / 2;
    state.input.mouse.currentPosition.y = h / 2;

    state.input.mouse.previousPosition.x = w / 2;
    state.input.mouse.previousPosition.y = h / 2;
    state.ui.overlay.on_screen_size_change( w, h );
    state.ui.font.onScreenSizeChange( w, h );
    // Sizing pass: every panel the Clay layout positions gets its grid
    // metrics first — compute_layout is pure (no GL rebuild).
    state.hotbar.inventory_renderer.compute_layout( w, h );
    state.survival_inventory.inventory_renderer.compute_layout( w, h );
    state.ui.creative.inventory_renderer.compute_layout( w, h );
    // Container grids share the survival stride — size them before the
    // pass so the declared slab sizes match.
    repgame_layout_containers( state );
    state.ui.layout.compute( ui_layout_input( state ) );
    // Adopt the measured rects: each slab lands where the layout put it
    // (the hotbar flush at the screen bottom, the survival inventory above
    // the HUD strip, the creative palette centered + floor-lifted, the
    // container screens floating over the survival slab).
    const UiLayoutRects &rects = state.ui.layout.rects( );
    state.hotbar.apply_layout( rects.hotbar );
    state.survival_inventory.apply_layout( rects.survival_items );
    state.ui.creative.apply_layout( w, h, rects.creative_items, rects.search_box );
    repgame_apply_ui_layout( state );
    state.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), state.screen.width / state.screen.height, 0.1f, CAMERA_FAR_PLANE );
    state.screen.ortho = glm::ortho<float>( 0.f, w, 0.f, h, -1.f, 1.f );
    state.screen.ortho_center = glm::ortho<float>( -w / 2.0f, w / 2.0f, -h / 2.0f, h / 2.0f, -1.f, 1.f );
    state.world.change_size( w, h );
}

void RepGame::changeSize( const int w, const int h ) {
    // pr_debug( "Screen Size Change:%dx%d", w, h );
    repgame_set_screen_size( globalGameState, w, h );
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