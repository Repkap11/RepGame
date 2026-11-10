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
    return !run_policy().capture.enabled && !globalGameState.input.inventory_open && !globalGameState.input.menu_open;
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
    globalGameState.ui.pause.onScreenSizeChange( w, h, globalGameState.ui.font );
    globalGameState.ui.font.onScreenSizeChange( w, h );
    globalGameState.ui.creative.onScreenSizeChange( w, h );
    // Align the survival panel's bottom edge with the creative panel's bottom,
    // and the chest panel's top edge with the creative panel's top, so the
    // combined chest+player-inventory view spans the creative panel's area.
    globalGameState.survival_inventory.inventory_renderer.options.pin_bottom_y = globalGameState.ui.creative.inventory_renderer.getInvBottom( );
    globalGameState.survival_inventory.onScreenSizeChange( w, h );
    globalGameState.hotbar.onScreenSizeChange( w, h );
    repgame_chest_layout( globalGameState );
    repgame_crafting_layout( globalGameState );
    repgame_furnace_layout( globalGameState );
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
    return !state.peaceful && !state.input.no_clip;
}

Input &RepGame::getInputState( ) {
    return globalGameState.input;
}