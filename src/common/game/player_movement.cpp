// RepGame member functions: camera orientation and movement physics.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/dimension_profile.hpp"
#include "common/fluid.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/schedule.hpp"
#include "common/game/game_internal.hpp"
#include "common/break_drops.hpp"
#include "common/rail.hpp"
#include "common/repeater.hpp"
#include "common/tree_grow.hpp"
#include "common/vehicle_physics.hpp" // VEHICLE_BOAT_DRAFT


void RepGame::build_camera_view( float angle_H, float angle_V, const glm::dvec3 &pos, glm::vec3 &look, glm::mat4 &rotation, glm::mat4 &view_look, glm::mat4 &view_trans ) const {
    look = glm::normalize( glm::vec3(        //
        sin( ( angle_H ) * ( M_PI / 180 ) ), //
        -tan( angle_V * ( M_PI / 180 ) ),    //
        -cos( ( angle_H ) * ( M_PI / 180 ) ) ) );

    glm::mat4 rotate = glm::rotate( glm::mat4( 1.0f ), glm::radians( -angle_H + 180 ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    rotate = glm::rotate( rotate, glm::radians( angle_V ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    rotation = rotate;

    view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                             look,                         // Look at look vector
                             glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
    );
    view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -pos ) );
}

void RepGame::process_camera_angle( ) {
    float upAngleLimit = 90 - 0.001f;
    if ( globalGameState.camera.angle_V > upAngleLimit ) {
        globalGameState.camera.angle_V = upAngleLimit;
    }
    float downAngleLimit = -90 + 0.001f;
    if ( globalGameState.camera.angle_V < downAngleLimit ) {
        globalGameState.camera.angle_V = downAngleLimit;
    }
    build_camera_view( globalGameState.camera.angle_H, globalGameState.camera.angle_V, globalGameState.camera.pos, //
                       globalGameState.camera.look, globalGameState.camera.rotation, globalGameState.camera.view_look, globalGameState.camera.view_trans );

    globalGameState.camera.movement.x = sinf( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * static_cast<float>( ( M_PI / 180 ) ) );
    globalGameState.camera.movement.y = -tanf( globalGameState.camera.angle_V * static_cast<float>( ( M_PI / 180 ) ) );
    globalGameState.camera.movement.z = -cosf( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * static_cast<float>( ( M_PI / 180 ) ) );
}

void RepGame::process_movement( ) {
    PROFILE_SCOPE( "player_movement" );

    if ( globalGameState.camera.riding_vehicle != 0 ) {
        // Riding a vehicle: no walk/swim/climb body physics — the seat
        // comes from repgame_vehicle_ride_follow after the vehicle tick. W/S
        // intent rides along as rider_push; a sneak press dismounts,
        // edge-gated so a sneak held through the mount click doesn't
        // eject instantly.
        if ( !globalGameState.input.movement.sneakPressed ) {
            globalGameState.camera.ride_sneak_seen_up = 1;
        } else if ( globalGameState.camera.ride_sneak_seen_up ) {
            repgame_vehicle_dismount( globalGameState );
            globalGameState.camera.rider_push = glm::dvec2( 0.0 );
            globalGameState.camera.rider_forward = 0.0;
            globalGameState.camera.rider_turn = 0.0;
            return;
        }
        const Vehicles::Vehicle *riding = globalGameState.world.vehicles.find( globalGameState.camera.riding_vehicle );
        if ( riding != nullptr && vehicle_type_def( riding->type ).physics == VehiclePhysics::WATER ) {
            // Boat controls (MC-style): W/S paddle along the boat's own
            // yaw, A/D steer — the camera stays free-look. angleH is the
            // movement direction (W=0, D=+90, S=180, A=-90).
            const double ah = glm::radians( static_cast<double>( globalGameState.input.movement.angleH ) );
            globalGameState.camera.rider_forward = cos( ah ) * globalGameState.input.movement.sizeH;
            globalGameState.camera.rider_turn = sin( ah ) * globalGameState.input.movement.sizeH;
            globalGameState.camera.rider_push = glm::dvec2( 0.0 );
        } else {
            globalGameState.camera.rider_push = glm::dvec2( globalGameState.camera.movement.x, globalGameState.camera.movement.z ) *
                                                static_cast<double>( globalGameState.input.movement.sizeH );
            globalGameState.camera.rider_forward = 0.0;
            globalGameState.camera.rider_turn = 0.0;
        }
        globalGameState.camera.standing_on_solid = 1;
        return;
    }
    globalGameState.camera.rider_push = glm::dvec2( 0.0 );
    globalGameState.camera.rider_forward = 0.0;
    globalGameState.camera.rider_turn = 0.0;

    bool player_flying = globalGameState.input.player_flying;
    bool player_sprinting = globalGameState.input.player_sprinting;

    // Swimming engages only when the body center is *below the surface plane*
    // of a liquid block — water tops out at WATER_HEIGHT within its block, so
    // in 1-deep water the center sits above the surface and the player wades
    // (slowed walk, normal jump) instead of swimming. camera.pos is the eye;
    // feet are eye_height below it. The swim check uses the STANDING body
    // center (feet + PLAYER_HEIGHT/2) so it is pose-independent — a crouched
    // player dipping their actual center below the surface must not flip into
    // swimming, which would cancel the sneak and oscillate every tick.
    const double body_center_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height + PLAYER_HEIGHT / 2.0;
    const glm::dvec3 body_center( globalGameState.camera.pos.x, body_center_y, globalGameState.camera.pos.z );
    const glm::ivec3 body_block_pos = glm::ivec3( glm::round( body_center - glm::dvec3( 0.5 ) ) );
    const BlockState body_block = globalGameState.world.get_loaded_block( body_block_pos );
    const bool body_in_liquid = body_block.id < LAST_BLOCK_ID && block_definition_get_definition( body_block.id )->flows != 0;
    // A water block is only partially full when it's the surface: if the block
    // above is also liquid, the column fills it completely.
    const BlockState above_block = globalGameState.world.get_loaded_block( body_block_pos + glm::ivec3( 0, 1, 0 ) );
    const bool liquid_above = above_block.id < LAST_BLOCK_ID && block_definition_get_definition( above_block.id )->flows != 0;
    // Sloped fluids sit lower than the source height — the surface lookup
    // handles level heights and full columns, falling back to WATER_HEIGHT
    // for non-fluid cells.
    const double surface_height = static_cast<double>( body_block_pos.y ) + ( body_in_liquid ? fluid_surface_height( globalGameState.world, body_block_pos ) : WATER_HEIGHT );
    const bool player_swimming = !player_flying && body_in_liquid && ( liquid_above || body_center.y < surface_height );
    // Wading: feet in a liquid but body above the surface (e.g. 1-deep water).
    const glm::ivec3 feet_block_pos = glm::ivec3( glm::round( globalGameState.camera.pos - glm::dvec3( 0.0, globalGameState.camera.eye_height - 0.05, 0.0 ) - glm::dvec3( 0.5 ) ) );
    const BlockState feet_block = globalGameState.world.get_loaded_block( feet_block_pos );
    const bool player_wading = !player_flying && !player_swimming && feet_block.id < LAST_BLOCK_ID && block_definition_get_definition( feet_block.id )->flows != 0;

    // Climbing (ladders): the player is "on a ladder" while the body AABB
    // overlaps a cell holding a climbable block. The check uses the standing
    // height (like the swim check's standing body center) so a sneak-pose
    // transition can't drop the grip mid-tick. Scaffolding is also climbable
    // but hangs/descends differently — the matched id tells the branch.
    BlockID climb_block = AIR;
    const bool player_climbing = !player_flying && !globalGameState.input.no_clip &&
                                 Collision::on_climbable( globalGameState.world, globalGameState.camera.pos, PLAYER_HEIGHT, globalGameState.camera.eye_height,
                                                          PLAYER_WIDTH / 2.0, &climb_block );
    const bool climb_is_scaffold = climb_block != LAST_BLOCK_ID && block_definition_get_definition( climb_block )->is_scaffolding;

    // Sneak pose transitions. The sneak key doubles as "move down" while
    // flying, and while swimming it crouches AND sinks (the swim branch below
    // still applies its sink assist). Feet stay planted: the eye
    // (camera.pos.y) shifts by the eye-height difference.
    const bool wants_sneak = globalGameState.input.movement.sneakPressed && !player_flying && !globalGameState.input.no_clip;
    if ( wants_sneak && !globalGameState.camera.sneaking ) {
        globalGameState.camera.sneaking = 1;
        globalGameState.camera.player_height = PLAYER_SNEAK_HEIGHT;
        globalGameState.camera.eye_height = PLAYER_SNEAK_EYE_HEIGHT;
        globalGameState.camera.pos.y -= PLAYER_SNEAK_EYE_DROP;
    } else if ( !wants_sneak && globalGameState.camera.sneaking ) {
        // Stand up only if the taller box has headroom; otherwise stay
        // crouched. Rechecked every tick, so the player stands as soon as
        // they walk out from under a low ceiling.
        const glm::dvec3 stand_eye = globalGameState.camera.pos + glm::dvec3( 0.0, PLAYER_SNEAK_EYE_DROP, 0.0 );
        if ( !Collision::collides_at( globalGameState.world, stand_eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) ) {
            globalGameState.camera.sneaking = 0;
            globalGameState.camera.player_height = PLAYER_HEIGHT;
            globalGameState.camera.eye_height = PLAYER_EYE_HEIGHT;
            globalGameState.camera.pos.y += PLAYER_SNEAK_EYE_DROP;
        }
    }
    // Ease the rendered eye height toward the pose target so the camera
    // glides instead of snapping; applied in draw(), not used by physics.
    globalGameState.camera.eye_height_render += ( globalGameState.camera.eye_height - globalGameState.camera.eye_height_render ) * 0.3;

    float gravity = player_flying ? 0 : ( player_swimming ? WATER_GRAVITY_STRENGTH : GRAVITY_STRENGTH );

    float movement_speed;
    if ( player_flying && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_FLYING_SPRINTING;
    } else if ( player_flying && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_FLYING;
    } else if ( player_swimming && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SWIMMING_SPRINTING;
    } else if ( player_swimming && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SWIMMING;
    } else if ( globalGameState.camera.sneaking && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SNEAKING_SPRINTING;
    } else if ( globalGameState.camera.sneaking ) {
        movement_speed = MOVEMENT_SENSITIVITY_SNEAKING;
    } else if ( player_wading && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_WADING_SPRINTING;
    } else if ( player_wading ) {
        movement_speed = MOVEMENT_SENSITIVITY_WADING;
    } else if ( !player_flying && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SPRINTING;
    } else if ( !player_flying && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_WALKING;
    } else {
        movement_speed = 0;
        pr_debug( "Invalid sprint fly combo" );
    }

    if ( player_flying ) {
        const float fly_vspeed = player_sprinting ? FLY_SPRINT_VERTICAL_SPEED : FLY_VERTICAL_SPEED;
        if ( globalGameState.input.movement.sneakPressed && globalGameState.input.movement.jumpPressed ) {
            globalGameState.camera.y_speed = 0;
        } else {
            if ( globalGameState.input.movement.sneakPressed ) {
                globalGameState.camera.y_speed = -fly_vspeed;
            } else if ( globalGameState.input.movement.jumpPressed ) {
                globalGameState.camera.y_speed = fly_vspeed;
            } else {
                globalGameState.camera.y_speed = 0;
            }
        }
    } else if ( player_climbing ) {
        // On a ladder (a cell whose block is "climbable"): gravity still
        // applies, so idle players slide down capped at CLIMB_SLIDE_SPEED
        // while upward momentum (jumping past the cell) is preserved.
        // Holding jump, or pushing FORWARD into a wall that blocks the move
        // (MC's horizontalCollision — input held while the collision-resolved
        // horizontal velocity is ~0), ascends; sneak hangs in place.
        // Strafe/back input into a wall does NOT climb: angleH is relative
        // to the camera yaw (W=0, D=+90, S=180, A=-90), so cos > 0.5 keeps
        // forward and the ±45 diagonals but drops side and back.
        const bool pushing = globalGameState.input.movement.sizeH > 0.001f &&
                             cosf( globalGameState.input.movement.angleH * static_cast<float>( M_PI / 180.0 ) ) > 0.5f &&
                             glm::length( globalGameState.camera.horizontal_vel ) < 0.01;
        if ( globalGameState.input.movement.jumpPressed || pushing ) {
            globalGameState.camera.y_speed = CLIMB_VERTICAL_SPEED;
        } else if ( globalGameState.input.movement.sneakPressed ) {
            // Sneak on scaffolding sinks at the slide speed inside the open
            // frame but never through a lip — the lip boxes stay solid for
            // a sneaking mover (collision.cpp); on a ladder it hangs.
            globalGameState.camera.y_speed = climb_is_scaffold ? -CLIMB_SLIDE_SPEED : 0;
            gravity = 0; // sneak is the vertical override — no gravity substep
        } else if ( climb_is_scaffold ) {
            // MC: standing inside the frame hangs in place — no idle slide.
            globalGameState.camera.y_speed = 0;
            gravity = 0;
        } else if ( globalGameState.camera.y_speed < -CLIMB_SLIDE_SPEED ) {
            globalGameState.camera.y_speed = -CLIMB_SLIDE_SPEED;
        }
    } else if ( player_swimming ) {
        // Vertical control is only jump (swim up) and sneak (sink faster) —
        // pitch does not steer, so forward input moves purely horizontally
        // like walking. Idle players drift toward a gentle sink: weak water
        // gravity plus the drag below settles y_speed near
        // -WATER_GRAVITY_STRENGTH / (1 - WATER_VERTICAL_DRAG).
        double swim_y_target = 0.0;
        if ( globalGameState.input.movement.jumpPressed ) {
            swim_y_target += SWIM_VERTICAL_SPEED;
        }
        if ( globalGameState.input.movement.sneakPressed ) {
            swim_y_target -= SWIM_SINK_SPEED;
        }
        if ( fabs( swim_y_target ) > 1e-4 ) {
            const double dy = swim_y_target - globalGameState.camera.y_speed;
            globalGameState.camera.y_speed += glm::clamp( dy, -static_cast<double>( PLAYER_WATER_ACCEL ), static_cast<double>( PLAYER_WATER_ACCEL ) );
        } else {
            globalGameState.camera.y_speed *= WATER_VERTICAL_DRAG;
        }
    } else {
        if ( globalGameState.input.movement.jumpPressed && globalGameState.camera.standing_on_solid && globalGameState.camera.y_speed < 0.01f && globalGameState.camera.y_speed > -0.01f ) {
            globalGameState.camera.y_speed = JUMP_STRENGTH;
        }
    }

    float accel = -gravity;

    // Build the target horizontal velocity from input. movement.x/z are the
    // unit direction derived from the camera yaw plus the stick's relative
    // angleH (angleH convention: W=0, D=+90, S=180, A=-90), so pitch never
    // steers — swimming moves on the yaw plane just like walking. sizeH is
    // the analog input magnitude (0..1 on keyboard). The target is clamped
    // to the selected movement speed so acceleration can never push us past
    // it.
    const float target_vx = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.x;
    const float target_vz = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.z;
    const glm::dvec2 target_vel( target_vx, target_vz );

    // Pick the rate at which horizontal_vel approaches target_vel. When input
    // is held we accelerate; when it is released we apply (usually higher)
    // friction to bring the player to rest. Flying uses a high rate so it
    // stays snappy and effectively reaches target in a single tick. Water
    // uses a low rate for a damped, drifting feel.
    const bool has_input = globalGameState.input.movement.sizeH > 0.001f;
    float rate;
    if ( player_flying ) {
        rate = PLAYER_FLY_ACCEL;
    } else if ( player_swimming ) {
        rate = has_input ? PLAYER_WATER_ACCEL : PLAYER_WATER_FRICTION;
    } else if ( has_input ) {
        rate = globalGameState.camera.standing_on_solid ? PLAYER_GROUND_ACCEL : PLAYER_AIR_ACCEL;
    } else {
        rate = globalGameState.camera.standing_on_solid ? PLAYER_GROUND_FRICTION : PLAYER_AIR_FRICTION;
    }

    // Accelerate horizontal_vel toward target_vel, clamping the per-tick
    // velocity change to `rate` so the approach is smooth.
    glm::dvec2 dvel = target_vel - globalGameState.camera.horizontal_vel;
    double dvel_len = glm::length( dvel );
    if ( dvel_len <= rate || rate <= 0.0f ) {
        globalGameState.camera.horizontal_vel = target_vel;
    } else {
        globalGameState.camera.horizontal_vel += dvel * ( rate / dvel_len );
    }

    glm::dvec3 movement_vector = glm::dvec3( );
    movement_vector.x = globalGameState.camera.horizontal_vel.x;
    movement_vector.y = globalGameState.camera.y_speed + accel;
    // Terminal velocity only applies to gravity-driven falling, not to flying
    // (which sets y_speed directly from input). Swimming has its own much
    // lower cap.
    if ( !player_flying ) {
        const double terminal = player_swimming ? SWIM_TERMINAL_VELOCITY : TERMINAL_VELOCITY;
        if ( movement_vector.y > terminal ) {
            movement_vector.y = terminal;
        }
        if ( movement_vector.y < -terminal ) {
            movement_vector.y = -terminal;
        }
    }
    movement_vector.z = globalGameState.camera.horizontal_vel.y;
    if ( globalGameState.input.no_clip ) {
        globalGameState.camera.standing_on_solid = 0;
    } else {
        Collision::check_move( globalGameState.world, movement_vector,                                     //
                               globalGameState.camera.pos,                                                 //
                               &globalGameState.camera.standing_on_solid,                                  //
                               globalGameState.camera.player_height, globalGameState.camera.eye_height,    //
                               globalGameState.camera.sneaking != 0, PLAYER_WIDTH / 2.0,
                               &globalGameState.world.bodies.piston_collision_boxes( ) );
    }

    if ( run_policy( ).walktest ) {
        fprintf( stderr, "WT %ld pos %.5f %.5f %.5f req %.5f %.5f applied %.5f %.5f %.5f yspeed %.5f standing %d angle %.2f\n", //
                 globalGameState.tick_number, globalGameState.camera.pos.x, globalGameState.camera.pos.y, globalGameState.camera.pos.z, //
                 globalGameState.camera.horizontal_vel.x, globalGameState.camera.horizontal_vel.y,                                     //
                 movement_vector.x, movement_vector.y, movement_vector.z,                                                              //
                 globalGameState.camera.y_speed, globalGameState.camera.standing_on_solid, globalGameState.camera.angle_H );
    }
    globalGameState.camera.pos = globalGameState.camera.pos + movement_vector;
    // A grounded player has no vertical velocity: the applied Y displacement
    // is either the gravity substep clamped to ~0, or — after a step-up onto a
    // slab/snow layer — the full step height (~+0.5). Letting the latter
    // become y_speed launches the player into the air, so reset it here. The
    // position update above still uses the full movement_vector, so the player
    // correctly ends up on top of the stepped block.
    if ( globalGameState.camera.standing_on_solid ) {
        globalGameState.camera.y_speed = 0.0f;
    } else {
        globalGameState.camera.y_speed = movement_vector.y;
    }
    // If a horizontal axis was blocked by collision, zero that component of
    // the persisted velocity so we don't keep pushing into the wall.
    globalGameState.camera.horizontal_vel.x = movement_vector.x;
    globalGameState.camera.horizontal_vel.y = movement_vector.z;
    // pr_debug("Y speed:%f",globalGameState.camera.y_speed);

    // Footsteps: while grounded, accumulate the actual horizontal distance
    // covered (collision-resolved, so pushing a wall stays silent) and play
    // the underfoot block's step sound every ~1.6 blocks — about MC's
    // cadence, ~3 steps/s at walk speed. A 0.2s minimum gap keeps sprint
    // (~18 blocks/s) from machine-gunning steps, and a 0.6s idle threshold
    // makes any motion after a pause count immediately. Any air-to-ground
    // transition also plays a step, so jumping lands with a footfall.
    {
        static double step_distance = 0.0;
        static bool was_grounded = false;
        static long last_step_tick = -1000;
        const bool grounded = globalGameState.camera.standing_on_solid && !player_flying && !globalGameState.input.no_clip;
        bool step_now = false;
        if ( grounded ) {
            step_distance += glm::length( glm::dvec2( movement_vector.x, movement_vector.z ) );
            const long since_step = globalGameState.tick_number - last_step_tick;
            const bool gap_ok = since_step >= ( long )( 0.2 * UPS_RATE );
            // A small motion after a long pause still counts as a step —
            // otherwise sub-1.6-block shuffles stay silent forever. 0.45
            // blocks is far enough into the motion that a single shuffle
            // reads as one step, not two.
            const bool resumed = step_distance >= 0.45 && since_step >= ( long )( 0.6 * UPS_RATE );
            if ( gap_ok && ( !was_grounded || step_distance >= 1.6 || resumed ) ) {
                step_now = true;
                step_distance = 0.0;
                last_step_tick = globalGameState.tick_number;
            }
        } else {
            step_distance = 0.0;
        }
        was_grounded = grounded;
        if ( step_now ) {
            const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
            const glm::ivec3 under_pos = glm::ivec3( glm::round( glm::dvec3( globalGameState.camera.pos.x, feet_y - 0.05, globalGameState.camera.pos.z ) - 0.5 ) );
            const BlockState under = globalGameState.world.get_loaded_block( under_pos );
            if ( under.id != AIR && under.id != LAST_BLOCK_ID && block_definition_get_definition( under.id )->flows == 0 ) {
                audio_play_sfx_gain( audio_block_step_pool( under.id ), 0.75f );
            }
        }
    }

    // Farmland trampling and fall damage share one fall tracker: landing
    // after real downward travel (>0.5 — a jump lands ~1.3, a step-up 0)
    // converts a field underfoot back to dirt and, past the safe distance,
    // hurts the player. Flying, no-clip and swimming don't count (same
    // model as the mob sim) — water entry resets the distance.
    {
        static bool was_grounded_trample = false;
        double &fall_dist = globalGameState.vitals.fall_dist;
        const bool grounded = globalGameState.camera.standing_on_solid && !player_flying && !globalGameState.input.no_clip;
        if ( grounded ) {
            if ( !was_grounded_trample && fall_dist > 0.5 ) {
                const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
                const glm::ivec3 under_pos = glm::ivec3( glm::round( glm::dvec3( globalGameState.camera.pos.x, feet_y - 0.05, globalGameState.camera.pos.z ) - 0.5 ) );
                if ( globalGameState.world.get_loaded_block( under_pos ).id == FARMLAND ) {
                    repgame_convert_block( globalGameState, under_pos, DIRT );
                }
                // MC's rule: floor(distance) - 3 half-hearts.
                const float fall_damage = static_cast<float>( floor( fall_dist ) ) - static_cast<float>( PLAYER_FALL_SAFE_DISTANCE );
                if ( RepGame::player_damage( globalGameState, fall_damage ) ) {
                    audio_play_sfx( fall_damage >= 8.0f ? "fall_big" : "fall_small" );
                }
            }
            fall_dist = 0.0;
        } else if ( player_swimming ) {
            fall_dist = 0.0;
        } else if ( movement_vector.y < 0.0 && !player_flying && !globalGameState.input.no_clip && !player_climbing ) {
            fall_dist += -movement_vector.y;
        }
        was_grounded_trample = grounded;
    }

    // Water: a splash on entry — heavy when falling in fast, and gain
    // scaled by entry speed so wading in quietly laps instead of booming
    // like a dive — and swim strokes every ~1.5 blocks of submerged
    // movement (3D distance, so diving straight down counts). Wading keeps
    // footsteps instead. The liquid checks below use the POST-movement
    // position (player_swimming / player_wading are computed pre-movement
    // and lag one tick — that delay is audible on the splash).
    {
        static bool was_in_liquid = false;
        static bool liquid_state_known = false;
        static double swim_distance = 0.0;
        static long last_swim_tick = -1000;
        static long last_liquid_tick = -1000;
        const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
        const BlockState feet_block_now = globalGameState.world.get_loaded_block( glm::ivec3( glm::round( glm::dvec3( globalGameState.camera.pos.x, feet_y + 0.05, globalGameState.camera.pos.z ) - 0.5 ) ) );
        const BlockState body_block_now = globalGameState.world.get_loaded_block( glm::ivec3( glm::round( body_center - 0.5 ) ) );
        const bool feet_in_liquid = feet_block_now.id < LAST_BLOCK_ID && block_definition_get_definition( feet_block_now.id )->flows != 0;
        const bool body_in_liquid_now = body_block_now.id < LAST_BLOCK_ID && block_definition_get_definition( body_block_now.id )->flows != 0;
        const bool in_liquid = ( feet_in_liquid || body_in_liquid_now ) && !player_flying && !globalGameState.input.no_clip;
        // Unloaded chunks read as LAST_BLOCK_ID: skip until they load, and take
        // the first loaded sample silently so spawning underwater doesn't splash.
        const bool blocks_loaded = feet_block_now.id != LAST_BLOCK_ID && body_block_now.id != LAST_BLOCK_ID;
        if ( blocks_loaded && !liquid_state_known ) {
            liquid_state_known = true;
            was_in_liquid = in_liquid;
        }
        if ( in_liquid ) {
            if ( !was_in_liquid ) {
                // Only a real dry spell counts as a fresh entry: flow writes
                // receding/refilling the cell underfoot toggle in_liquid as
                // the shape settles, and each rewrite used to re-splash.
                const long dry_ticks = globalGameState.tick_number - last_liquid_tick;
                const double entry_speed = glm::length( movement_vector );
                if ( dry_ticks >= ( long )( 0.5 * UPS_RATE ) && entry_speed > 0.01 ) {
                    const float gain = glm::clamp( (float)( entry_speed / 0.35 ), 0.05f, 1.0f );
                    audio_play_sfx_gain( -movement_vector.y > 0.25 ? "heavy_splash" : "splash", gain );
                }
                swim_distance = 0.0;
                last_swim_tick = globalGameState.tick_number;
            } else if ( body_in_liquid_now ) {
                swim_distance += glm::length( movement_vector );
                const long since_swim = globalGameState.tick_number - last_swim_tick;
                if ( swim_distance >= 1.5 && since_swim >= ( long )( 0.3 * UPS_RATE ) ) {
                    audio_play_sfx( "swim" );
                    swim_distance = 0.0;
                    last_swim_tick = globalGameState.tick_number;
                }
            }
        } else {
            swim_distance = 0.0;
        }
        if ( blocks_loaded ) {
            was_in_liquid = in_liquid;
            if ( in_liquid ) {
                last_liquid_tick = globalGameState.tick_number;
            }
        }
    }

    // Head-level water transitions get their own cues on top of the feet
    // splash: an enter/exit sting, a retriggered ambience bed while the head
    // stays under, and sparse "additions" sprinkled on top. The ambience
    // clip is ~60 s; the additions ride a short random timer.
    {
        static int head_water_state = -1; // -1 unknown (spawn), 0 dry, 1 wet
        static long next_addition_tick = 0;
        static long next_ambience_tick = 0;
        const BlockState eye_block_now = globalGameState.world.get_loaded_block(
            glm::ivec3( glm::round( globalGameState.camera.pos - glm::dvec3( 0.5 ) ) ) );
        if ( eye_block_now.id != LAST_BLOCK_ID ) {
            const int head_wet = fluid_in_cell( eye_block_now ) == WATER && !globalGameState.input.no_clip ? 1 : 0;
            if ( head_water_state >= 0 && head_wet != head_water_state ) {
                audio_play_sfx( head_wet ? "underwater_enter" : "underwater_exit" );
                next_ambience_tick = globalGameState.tick_number;
            }
            head_water_state = head_wet;
            if ( head_wet ) {
                if ( globalGameState.tick_number >= next_ambience_tick ) {
                    audio_play_sfx_gain( "underwater_ambience", 0.6f );
                    next_ambience_tick = globalGameState.tick_number + 55 * UPS_RATE;
                }
                if ( globalGameState.tick_number >= next_addition_tick ) {
                    audio_play_sfx_gain( "underwater_addition", 0.7f );
                    next_addition_tick = globalGameState.tick_number + ( 4 + rand( ) % 10 ) * UPS_RATE;
                }
            } else {
                next_addition_tick = globalGameState.tick_number;
            }
        }
    }
}
