#include "common/piston_push.hpp"

#include "common/RepGame.hpp"
#include "common/utils/collision.hpp"
#include "common/world.hpp"

void repgame_piston_push_entities( RepGameState &state ) {
    World &world = state.world;
    MovingBoxPushSet push;
    world.bodies.collect_piston_push_boxes( push.movers );
    push.collision.reserve( push.movers.size( ) );
    for ( const MovingBodyBox &moving : push.movers ) {
        push.collision.push_back( moving.box );
    }
    // Capture mode deliberately freezes entity motion. Still consume final
    // landing motions so they cannot accumulate or fire when capture ends.
    const bool apply = !RepGame::capture_mode( ) && !push.movers.empty( );
    if ( apply ) {
        // The local player is authoritative for itself. As with crowding,
        // the correction is positional only: input velocity, knockback,
        // fall-distance accounting, and no-clip state are left untouched.
        if ( !state.input.no_clip && state.camera.riding_vehicle == 0 && !state.vitals.dead && !state.spawn_pending ) {
            const double half = PLAYER_WIDTH * 0.5;
            glm::dvec3 min( state.camera.pos.x - half, state.camera.pos.y - state.camera.eye_height, state.camera.pos.z - half );
            glm::dvec3 max( state.camera.pos.x + half, min.y + state.camera.player_height, state.camera.pos.z + half );
            const glm::dvec3 before = min;
            for ( const MovingBodyBox &moving : push.movers ) {
                Collision::apply_moving_box_push( world, moving, min, max, push.collision );
            }
            state.camera.pos += min - before;
        }
        world.mobs.push_with_moving_boxes( world, push );
        world.bodies.push_with_moving_boxes( world, push );
        world.items.push_with_moving_boxes( world, push );
    }
    world.bodies.consume_final_piston_pushes( );
}
