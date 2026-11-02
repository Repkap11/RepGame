#pragma once

// Helpers shared by the RepGame member-function implementations split
// across src/common/game/. Internal to those translation units — not part
// of the RepGame API.

#include <chrono>
#include <stdlib.h>
#include <glm/glm.hpp>

struct RepGameState;
class InventoryRenderer;

// Returns a closed crafting grid's contents to the player (defined in
// inventory.cpp, also called by the tick's table-distance check and by
// cleanup() before the player save).
void repgame_crafting_return_grid( RepGameState &state );

// Crafting-grid layout/geometry shared by the inventory UI, the mouse
// handler, changeSize(), and the overlay draw (all in inventory.cpp).
int repgame_crafting_dim( const RepGameState &state );
bool repgame_crafting_grid_visible( const RepGameState &state );
InventoryRenderer &repgame_crafting_grid( RepGameState &state );
void repgame_crafting_layout( RepGameState &state );

static inline long long repgame_now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

// Launch velocity (blocks/tick at UPS_RATE) for block-break and mob drops: a
// small pop that peaks ~0.2-0.35 blocks up and drifts <0.3 sideways under the
// item gravity/drag, so drops fall back roughly where they spawned.
static inline glm::dvec3 repgame_item_drop_hop( ) {
    const auto r = [ ]( ) { return rand( ) / ( double )RAND_MAX; };
    return glm::dvec3( ( r( ) - 0.5 ) * 0.008, 0.018 + r( ) * 0.007, ( r( ) - 0.5 ) * 0.008 );
}

// VR UI quad placement (defined in vr.cpp; tick() uses them for the laser
// pointer math).
float repgame_vr_ui_dist( );
glm::vec2 repgame_vr_ui_size( float screen_w, float screen_h );
