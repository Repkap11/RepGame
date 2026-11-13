// Minecart riding: mount/dismount/seat-follow for the local player.
// Free functions off RepGame.hpp so rep_tests and the script harness
// drive the same code as the click path.

#include "common/RepGame.hpp"

#include <cmath>

#include "common/minecarts.hpp"
#include "common/utils/collision.hpp"
#include "common/world.hpp"

// Eye height above the cart's bottom-centre while riding: roughly MC's
// ~1.3 above the rail, so the wall tops (~0.66) fall below the view
// frustum when looking near-level and at most a thin rim strip shows.
static constexpr double CART_RIDER_EYE_OFFSET = 1.3;

bool repgame_cart_mount( RepGameState &gs, uint32_t cart_id ) {
    if ( gs.camera.riding_cart != 0 || cart_id == 0 || gs.world.carts.find( cart_id ) == nullptr ) {
        return false;
    }
    gs.camera.riding_cart = cart_id;
    // A sneak already held must be released once before it can dismount.
    gs.camera.ride_sneak_seen_up = gs.input.movement.sneakPressed ? 0 : 1;
    gs.camera.y_speed = 0.0;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.camera.rider_push = glm::dvec2( 0.0 );
    gs.vitals.fall_dist = 0.0;
    repgame_cart_ride_follow( gs );
    return true;
}

void repgame_cart_dismount( RepGameState &gs ) {
    Minecarts::Cart *cart = gs.world.carts.find( gs.camera.riding_cart );
    gs.camera.riding_cart = 0;
    if ( cart == nullptr ) {
        return;
    }
    const double yaw = glm::radians( static_cast<double>( cart->yaw ) );
    const glm::dvec3 fwd( sin( yaw ), 0.0, -cos( yaw ) );
    const glm::dvec3 side( -fwd.z, 0.0, fwd.x );
    const glm::ivec3 base( glm::floor( cart->pos ) );
    // Pick the first spot the player box fits: the two cells perpendicular
    // to travel, then forward/back along it, then on top of the cart.
    const glm::dvec3 dirs[ 4 ] = { side, -side, fwd, -fwd };
    glm::dvec3 feet;
    bool found = false;
    for ( int i = 0; i < 4; i++ ) {
        const glm::ivec3 cell = base + glm::ivec3( glm::round( dirs[ i ] ) );
        const glm::dvec3 cand( cell.x + 0.5, base.y + 0.0, cell.z + 0.5 );
        if ( !Collision::box_collides( gs.world, cand - glm::dvec3( PLAYER_WIDTH / 2.0, 0.0, PLAYER_WIDTH / 2.0 ),
                                       cand + glm::dvec3( PLAYER_WIDTH / 2.0, gs.camera.player_height, PLAYER_WIDTH / 2.0 ) ) ) {
            feet = cand;
            found = true;
            break;
        }
    }
    if ( !found ) {
        feet = cart->pos + glm::dvec3( 0.0, Minecarts::CART_HEIGHT, 0.0 );
    }
    gs.camera.pos = feet + glm::dvec3( 0.0, gs.camera.eye_height, 0.0 );
    gs.camera.y_speed = 0.0;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.vitals.fall_dist = 0.0;
    // No render blend from the seat to the dismount spot.
    gs.camera.snapshot_prev( );
    // Multiplayer: parked-state + DISMOUNT on the wire; the cart stays
    // locally simulated until the server's COUNT=0 lands.
    gs.world.carts.local_dismount( gs, cart );
}

void repgame_cart_ride_follow( RepGameState &gs ) {
    if ( gs.camera.riding_cart == 0 ) {
        return;
    }
    Minecarts::Cart *cart = gs.world.carts.find( gs.camera.riding_cart );
    if ( cart == nullptr ) {
        // The ridden cart broke, unloaded or was persisted — drop the
        // riding state and leave the camera where it is.
        gs.camera.riding_cart = 0;
        return;
    }
    gs.camera.pos = cart->pos + glm::dvec3( 0.0, CART_RIDER_EYE_OFFSET, 0.0 );
    gs.camera.y_speed = 0.0;
    gs.camera.horizontal_vel = glm::dvec2( 0.0 );
    gs.vitals.fall_dist = 0.0;
}
