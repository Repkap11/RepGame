// RepGame::tick — one simulation step.

#include <cmath>
#include <ctime>
#include <string>
#include <unistd.h>
#include <chrono>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/renderer/frame_buffer.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/utils/env_config.hpp"
#include "common/light.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/game/game_internal.hpp"

void RepGame::tick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    // VR controller state lands on the shared Input fields before anything
    // else reads them this tick.
    if ( globalGameState.vr.active && !run_policy().capture.enabled && !run_policy().fly.enabled ) {
        process_vr_input( );

        // UI quad anchor: opening the inventory (any source) pins the panel
        // in LOCAL space in front of wherever the head is looking (yaw
        // only); closing it hands it back to head-locked mode.
        const bool inv_open = globalGameState.input.inventory_open;
        if ( inv_open && !globalGameState.vr.prev_inventory_open ) {
            const glm::vec3 f = globalGameState.vr.head * glm::vec3( 0.0f, 0.0f, -1.0f );
            const float h = atan2f( f.x, -f.z );
            globalGameState.vr.ui_orientation = glm::angleAxis( -h, glm::vec3( 0.0f, 1.0f, 0.0f ) );
            globalGameState.vr.ui_position = globalGameState.vr.head_pos + globalGameState.vr.ui_orientation * glm::vec3( 0.0f, -0.15f, -repgame_vr_ui_dist( ) );
            globalGameState.vr.ui_world_locked = true;
        } else if ( !inv_open && globalGameState.vr.prev_inventory_open ) {
            globalGameState.vr.ui_world_locked = false;
        }
        globalGameState.vr.prev_inventory_open = inv_open;

        // Laser pointer: intersect the aim ray with the world-locked UI
        // quad so this tick's click handling sees this tick's position.
        globalGameState.vr.pointer_on_ui = false;
        if ( inv_open && globalGameState.vr.ui_world_locked && globalGameState.vr.input.aim_valid ) {
            const glm::vec2 ui_size = repgame_vr_ui_size( globalGameState.screen.width, globalGameState.screen.height );
            const glm::vec3 o = globalGameState.vr.input.aim_position;
            const glm::vec3 d = globalGameState.vr.input.aim_orientation * glm::vec3( 0.0f, 0.0f, -1.0f );
            const glm::vec3 n = globalGameState.vr.ui_orientation * glm::vec3( 0.0f, 0.0f, 1.0f );
            const float denom = glm::dot( d, n );
            if ( fabsf( denom ) > 1e-4f ) {
                const float t = glm::dot( globalGameState.vr.ui_position - o, n ) / denom;
                if ( t > 0.0f ) {
                    const glm::vec3 q = glm::inverse( globalGameState.vr.ui_orientation ) * ( o + t * d - globalGameState.vr.ui_position );
                    const float u = q.x / ui_size.x + 0.5f;
                    const float v = q.y / ui_size.y + 0.5f;
                    if ( u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f ) {
                        // Top-left origin, y down — same as SDL mouse.
                        globalGameState.input.mouse.absPosition.x = static_cast<int>( u * globalGameState.screen.width );
                        globalGameState.input.mouse.absPosition.y = static_cast<int>( ( 1.0f - v ) * globalGameState.screen.height );
                        globalGameState.vr.pointer_on_ui = true;
                    }
                }
            }
        }
    }
    globalGameState.tick_number++;
    // Music scheduling + one-shot SFX reaping (no-op without REPGAME_AUDIO).
    audio_tick( globalGameState.tick_number );
    // Feed the tick to the light pool: the BFS starve window measures backlog
    // age in ticks so the seed hot paths never read the clock.
    globalGameState.world.chunkLoader.light_note_tick( globalGameState.tick_number );
    // Day/night clock; light_daylight_factor() maps it to u_Daylight.
    globalGameState.world_time++;

    // Fresh spawn: the terrain-height estimate in initializeGameState can't
    // see structure blocks (trees, features). Once every cell in the spawn
    // column reports real generated data, stand on the topmost colliding
    // block so the player never starts inside a trunk or spire.
    if ( globalGameState.spawn_pending ) {
        const int top_bound = (int)ceil( MapGen::maxTerrainHeight( ) );
        for ( int y = top_bound; y >= BEDROCK_LEVEL; y-- ) {
            const BlockState bs = globalGameState.world.get_loaded_block( glm::ivec3( 0, y, 0 ) );
            if ( bs.id == LAST_BLOCK_ID ) {
                break; // Some chunk in the column isn't generated yet — retry next tick.
            }
            if ( block_definition_get_definition( bs.id )->collides_with_player ) {
                globalGameState.camera.pos.x = 0.5;
                globalGameState.camera.pos.z = 0.5;
                globalGameState.camera.pos.y = y + 1.0 + PLAYER_EYE_HEIGHT + 0.5;
                globalGameState.camera.y_speed = 0.0;
                globalGameState.spawn_pending = false;
                break;
            }
        }
    }

    // Capture mode: pin the simulation to the current view each tick, before
    // process_camera_angle/process_movement consume the camera/input state.
    if ( RepGame::capture_mode( ) ) {
        const CaptureView &view = run_policy().capture.views[ run_policy().capture.view_index ];
        globalGameState.world_time = run_policy().capture.world_time;
        globalGameState.input.worldDrawQuality = run_policy().capture.quality;
        globalGameState.camera.pos = glm::dvec3( view.x, view.y, view.z );
        globalGameState.camera.angle_H = view.yaw;
        globalGameState.camera.angle_V = view.pitch;
        globalGameState.input.player_flying = true;
        globalGameState.input.no_clip = true;
        globalGameState.camera.y_speed = 0.0;
        globalGameState.input.movement.sizeH = 0.0f;
        globalGameState.input.movement.jumpPressed = false;
        globalGameState.input.movement.sneakPressed = false;
        if ( run_policy().capture.applied_view != run_policy().capture.view_index ) {
            // New view: sync the interpolation snapshot so draw() doesn't blend
            // across the teleport from the previous view.
            run_policy().capture.applied_view = run_policy().capture.view_index;
            globalGameState.camera.prev_pos = globalGameState.camera.pos;
            globalGameState.camera.prev_eye_height = globalGameState.camera.eye_height;
            globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
            globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;
        }
    }

    // Snapshot the camera transform as it was at the end of the previous tick, so the
    // render loop can interpolate between this and the freshly-computed transform below.
    globalGameState.camera.prev_pos = globalGameState.camera.pos;
    globalGameState.camera.prev_eye_height = globalGameState.camera.eye_height;
    globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
    globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;

    globalGameState.multiplayer.process_events( globalGameState );

    int wheel_diff = globalGameState.input.mouse.previousPosition.wheel_counts - globalGameState.input.mouse.currentPosition.wheel_counts;
    if ( wheel_diff != 0 && !globalGameState.input.menu_open ) {
        if ( globalGameState.input.inventory_open && globalGameState.game_mode == GameMode_Creative ) {
            globalGameState.ui.creative.incrementSelectedPage( wheel_diff );
        } else if ( globalGameState.input.inventory_open && globalGameState.game_mode == GameMode_Survival ) {
            ItemID holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
            globalGameState.ui.overlay.set_holding_item( holdingBlock );
        } else if ( !globalGameState.input.inventory_open ) {
            ItemID holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
            globalGameState.ui.overlay.set_holding_item( holdingBlock );
        }
    }

    // Apply pending block updates before recomputing the selection so the
    // cursor reflects the post-destruction world. Player clicks queue events
    // tagged with the current tick, which processAllBlockUpdates defers until
    // the next tick (it skips events with tick_number >= current_tick); so
    // the destroy queued last tick is applied here, before the ray cast below.
    RepGame::process_block_updates( );

    // Close an open chest when the block is gone (broken by anyone), its
    // chunk unloaded, or the player walked out of reach.
    if ( globalGameState.ui.chest.open ) {
        const BlockState chest_state = globalGameState.world.get_loaded_block( globalGameState.ui.chest.pos );
        const glm::dvec3 chest_center = glm::dvec3( globalGameState.ui.chest.pos ) + glm::dvec3( 0.5 );
        const double dist = glm::length( globalGameState.camera.pos - chest_center );
        if ( !block_is_chest( chest_state.id ) || dist > REACH_DISTANCE + 2.0 ) {
            this->close_chest( );
            globalGameState.input.inventory_open = false;
        }
    }

    // Same for an open crafting table: closes when the block is gone (broken
    // by anyone), its chunk unloaded, or the player walks out of reach.
    if ( globalGameState.ui.crafting.table_open ) {
        const BlockState table_state = globalGameState.world.get_loaded_block( globalGameState.ui.crafting.table_pos );
        const glm::dvec3 table_center = glm::dvec3( globalGameState.ui.crafting.table_pos ) + glm::dvec3( 0.5 );
        const double dist = glm::length( globalGameState.camera.pos - table_center );
        if ( table_state.id != CRAFTING_TABLE || dist > REACH_DISTANCE + 2.0 ) {
            repgame_crafting_return_grid( globalGameState );
            globalGameState.input.inventory_open = false;
        }
    }

    if ( RepGame::should_lock_pointer( ) || run_policy().capture.enabled ) {
        if ( !run_policy().capture.enabled ) {
            RepGame::process_mouse_events( );
        }
        // The raycast also runs in capture mode so the selection box matches
        // what was on screen when the view was F12'd (the camera is pinned
        // above, so the ray is deterministic).
        int whichFace = 0;
        glm::dvec3 ray_origin = globalGameState.camera.pos;
        glm::dvec3 pos_with_reach = ray_origin + ( glm::dvec3( globalGameState.camera.look ) * static_cast<double>( REACH_DISTANCE ) );
        if ( globalGameState.vr.active && globalGameState.vr.input.aim_valid ) {
            // The right controller's aim pose (tracking space) becomes the
            // selection ray, rebased into world space by the body yaw.
            const glm::mat4 R_body = glm::rotate( glm::mat4( 1.0f ), glm::radians( -globalGameState.vr.body_yaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
            ray_origin = globalGameState.camera.pos + glm::dvec3( glm::vec3( R_body * glm::vec4( globalGameState.vr.input.aim_position, 0.0f ) ) );
            const glm::vec3 dir = glm::normalize( glm::vec3( R_body * glm::vec4( globalGameState.vr.input.aim_orientation * glm::vec3( 0.0f, 0.0f, -1.0f ), 0.0f ) ) );
            pos_with_reach = ray_origin + glm::dvec3( dir ) * static_cast<double>( REACH_DISTANCE );
        }
        // Keep the ray for the mob hit test in process_mouse_events (next
        // tick; the ray is recomputed every tick so it's at most 1 tick old).
        globalGameState.block_selection.ray_origin = ray_origin;
        globalGameState.block_selection.ray_dir = glm::normalize( pos_with_reach - ray_origin );
        const ItemID selected_block = globalGameState.hotbar.getSelectedItem( );
        const int pick_water = item_is_block( selected_block ) && block_definition_get_definition( static_cast<BlockID>( selected_block ) )->can_float_on_water;
        globalGameState.block_selection.selectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, nullptr, ray_origin, pos_with_reach, globalGameState.block_selection.pos_destroy, &whichFace, 0, 1, 0, 0 );

        globalGameState.block_selection.face = whichFace;
        globalGameState.block_selection.pos_create.x = globalGameState.block_selection.pos_destroy.x + ( whichFace == FACE_RIGHT ) - ( whichFace == FACE_LEFT );
        globalGameState.block_selection.pos_create.y = globalGameState.block_selection.pos_destroy.y + ( whichFace == FACE_TOP ) - ( whichFace == FACE_BOTTOM );
        globalGameState.block_selection.pos_create.z = globalGameState.block_selection.pos_destroy.z + ( whichFace == FACE_BACK ) - ( whichFace == FACE_FRONT );

        if ( pick_water ) {
            int whichFace_water = 0;
            glm::ivec3 pos_destroy_water;
            globalGameState.block_selection.floatSelectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, nullptr, ray_origin, pos_with_reach, pos_destroy_water, &whichFace_water, 0, 1, 0, 1 );
            globalGameState.block_selection.pos_create_water.x = pos_destroy_water.x + ( whichFace_water == FACE_RIGHT ) - ( whichFace_water == FACE_LEFT );
            globalGameState.block_selection.pos_create_water.y = pos_destroy_water.y + ( whichFace_water == FACE_TOP ) - ( whichFace_water == FACE_BOTTOM );
            globalGameState.block_selection.pos_create_water.z = pos_destroy_water.z + ( whichFace_water == FACE_BACK ) - ( whichFace_water == FACE_FRONT );
        } else {
            globalGameState.block_selection.floatSelectionInBounds = 0;
        }

        // A mob under the crosshair beats the block behind/under it: clears
        // the block selection so the outline (and any place/pick) doesn't
        // reach through the mob.
        RepGame::resolve_target_mob( globalGameState, ray_origin );
        globalGameState.world.set_selected_block( globalGameState.block_selection.pos_destroy, globalGameState.block_selection.selectionInBounds );
        if ( !run_policy().capture.enabled ) {
            const float raw_dx = static_cast<float>( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x );
            const float raw_dy = static_cast<float>( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y );
            // Low-pass filter the per-tick look delta so that sub-pixel / slow-pan motion
            // (where integer mouse deltas alternate 0,1,0,1) becomes a steady fractional
            // velocity instead of a stuttered step. Adds a small amount of input latency.
            globalGameState.input.mouse.smoothed_dx = globalGameState.input.mouse.smoothed_dx * ( 1.0f - MOUSE_SMOOTHING ) + raw_dx * MOUSE_SMOOTHING;
            globalGameState.input.mouse.smoothed_dy = globalGameState.input.mouse.smoothed_dy * ( 1.0f - MOUSE_SMOOTHING ) + raw_dy * MOUSE_SMOOTHING;
            if ( globalGameState.vr.active ) {
                // VR: the mouse turns the body, not the head — horizontal
                // motion maps to body_yaw (the head's own orientation comes
                // from the HMD), vertical motion does nothing.
                globalGameState.vr.body_yaw += globalGameState.input.mouse.smoothed_dx * MOUSE_SENSITIVITY;
                if ( globalGameState.vr.body_yaw >= 360.0f ) {
                    globalGameState.vr.body_yaw -= 360.0f;
                }
                if ( globalGameState.vr.body_yaw < 0.0f ) {
                    globalGameState.vr.body_yaw += 360.0f;
                }
            } else {
                globalGameState.camera.angle_H += globalGameState.input.mouse.smoothed_dx * MOUSE_SENSITIVITY;
                globalGameState.camera.angle_V += globalGameState.input.mouse.smoothed_dy * MOUSE_SENSITIVITY;
                if ( globalGameState.camera.angle_H >= 360.0f ) {
                    globalGameState.camera.angle_H -= 360.0f;
                }
                if ( globalGameState.camera.angle_H < 0.0f ) {
                    globalGameState.camera.angle_H += 360.0f;
                }
            }
        }
    } else {
        // Pointer unlocked (inventory open): abandon any in-progress mining.
        globalGameState.block_mining.progress_ticks = 0.0f;
    }

    // Feed mining progress to the selection overlay so the block shows crack
    // damage while the left button is held.
    float break_progress = 0.0f;
    if ( globalGameState.block_mining.progress_ticks > 0.0f ) {
        const float break_ticks = block_definition_get_definition( globalGameState.block_mining.id )->hardness * static_cast<float>( UPS_RATE );
        break_progress = break_ticks > 0.0f ? globalGameState.block_mining.progress_ticks / break_ticks : 0.0f;
    }
    globalGameState.world.mouseSelection.set_break_progress( break_progress );

    globalGameState.input.mouse.currentPosition.x = globalGameState.screen.width / 2.0f;
    globalGameState.input.mouse.currentPosition.y = globalGameState.screen.height / 2.0f;

    if ( run_policy( ).walktest ) {
        // REPGAME_WALKTEST_TICKS=n: quit after n ticks (timeout/SIGTERM gets
        // swallowed by SDL, so cap the test length deterministically). Go
        // through the normal exit path so cleanup() joins the terrain
        // threads — exit() here tears down libamdhip64 while a worker may be
        // inside map_gen_load_block_hip, segfaulting during shutdown.
        if ( run_policy( ).walktest_ticks && globalGameState.tick_number >= run_policy( ).walktest_ticks ) {
            globalGameState.input.exitGame = true;
        }
        // REPGAME_TELEPORT="x,y,z,angle,sprint,spin_deg_per_tick[,pitch]": teleport on
        // the first tick, then hold forward each tick (angle optionally spins).
        const char *tp = run_policy( ).teleport;
        if ( tp ) {
            float tx, ty, tz, ta, spin = 0.0f, pitch = 0.0f;
            int sprint = 0;
            if ( sscanf( tp, "%f,%f,%f,%f,%d,%f,%f", &tx, &ty, &tz, &ta, &sprint, &spin, &pitch ) >= 4 ) {
                if ( globalGameState.tick_number == 1 ) {
                    globalGameState.camera.pos = glm::dvec3( tx, ty, tz );
                    globalGameState.camera.angle_V = pitch;
                    globalGameState.camera.y_speed = 0.0f;
                    globalGameState.input.player_sprinting = sprint != 0;
                }
                globalGameState.camera.angle_H = ta + spin * globalGameState.tick_number;
            }
        }
        // REPGAME_SCREENSHOT_TICK=n: request a framebuffer screenshot on tick n
        // (screenshots land in ./screenshots/).
        if ( run_policy( ).screenshot_tick > 0 && globalGameState.tick_number == run_policy( ).screenshot_tick ) {
            globalGameState.input.screenshot_requested = true;
        }
        globalGameState.input.movement.sizeH = 1.0f;
        globalGameState.input.movement.angleH = 0.0f;
        globalGameState.input.movement.jumpPressed = false;
        globalGameState.input.movement.sneakPressed = false;
    }
    // Fly mode: scripted no-clip flight over fresh terrain (metrics-only —
    // the camera itself is scripted just like capture mode). Flags and view
    // angles go in here; the position pin lands right after process_movement
    // so physics can't fight it.
    if ( run_policy().fly.enabled && !run_policy().capture.enabled ) {
        globalGameState.camera.angle_H = run_policy().fly.yaw;
        globalGameState.camera.angle_V = run_policy().fly.pitch;
        globalGameState.input.player_flying = true;
        globalGameState.input.no_clip = true;
        globalGameState.camera.y_speed = 0.0;
        globalGameState.input.movement.sizeH = 0.0f;
        globalGameState.input.movement.jumpPressed = false;
        globalGameState.input.movement.sneakPressed = false;
    }
    // VR: fold the tracked head pose into the camera angles so block
    // raycasts, movement direction and remote-avatar facing all follow where
    // the head actually points. body_yaw (mouse) supplies the yaw base.
    if ( globalGameState.vr.active && !run_policy().capture.enabled && !run_policy().fly.enabled ) {
        const glm::vec3 f = globalGameState.vr.head * glm::vec3( 0.0f, 0.0f, -1.0f );
        const float head_yaw = glm::degrees( atan2f( f.x, -f.z ) );
        const float head_pitch = glm::degrees( -asinf( glm::clamp( f.y, -1.0f, 1.0f ) ) );
        float angle = globalGameState.vr.body_yaw + head_yaw;
        while ( angle >= 360.0f ) {
            angle -= 360.0f;
        }
        while ( angle < 0.0f ) {
            angle += 360.0f;
        }
        globalGameState.camera.angle_H = angle;
        globalGameState.camera.angle_V = head_pitch;
    }
    // Refresh camera.movement (and the view matrices) from the current angle_H
    // before consuming it: process_movement builds the player's velocity from
    // camera.movement, so it must reflect this tick's angle_H rather than the
    // previous tick's. Running camera angle first keeps the walk direction in
    // sync with the look direction.
    RepGame::process_camera_angle( );
    RepGame::process_movement( );
    // Mob physics + wander/chase AI run on the same tick cadence as the
    // player. Frozen in capture mode and when the feet chunk isn't live.
    // The input marks the player targetable only in walking survival, and
    // the output carries melee hits + creeper explosions back here.
    MobTickInput mob_in;
    mob_in.player_targetable = globalGameState.game_mode == GameMode_Survival && !globalGameState.input.no_clip;
    mob_in.world_time = globalGameState.world_time;
    mob_in.player_eye_height = PLAYER_EYE_HEIGHT - ( globalGameState.input.movement.sneakPressed ? PLAYER_SNEAK_EYE_DROP : 0.0f );
    // Owned hostiles also consider remote players (positions + targetable
    // flags come from the PLAYER_LOCATION stream).
    if ( globalGameState.multiplayer.multiplayer_mode( ) ) {
        globalGameState.world.multiplayer_avatars.fill_remote_targets( mob_in.remote_targets );
    }
    MobTickOutput mob_out;
    globalGameState.world.mobs.tick( globalGameState.world, globalGameState.camera.pos, &mob_in, &mob_out );
    // Falling blocks + primed TNT bodies integrate on the same cadence.
    globalGameState.world.bodies.tick( globalGameState );
    // Dropped item entities: physics, merge, magnet/pickup, despawn.
    globalGameState.world.items.tick( globalGameState );
    // Entity sync: apply pending role grants, advertise the VIEW, stream
    // ENTITY_STATE for owned entities. No-op in single player.
    globalGameState.multiplayer.entities.tick( globalGameState );
    for ( const MobPlayerHit &hit : mob_out.player_hits ) {
        if ( hit.target_player_id < 0 ) {
            RepGame::player_hurt( globalGameState, hit.from, hit.damage );
        } else {
            // An owned hostile connected on a remote player — forward it.
            globalGameState.multiplayer.entities.send_player_hit( hit.target_player_id, hit.from, hit.damage );
        }
    }
    for ( const glm::dvec3 &pos : mob_out.explosions ) {
        globalGameState.blockUpdateQueue.addBlockUpdate(
            std::make_shared<TntExplodeEvent>( globalGameState.tick_number, glm::ivec3( glm::floor( pos + glm::dvec3( 0.0, 0.5, 0.0 ) ) ), 0, false ) );
    }
    RepGame::process_pressure_plates( );
    if ( run_policy().fly.enabled && !run_policy().capture.enabled ) {
        // Settle gate: hold at the start position until the loader and light
        // queues have been idle REPGAME_FLY_SETTLE consecutive ticks (or the
        // timeout expires) so the flight starts from a converged baseline.
        const auto loader_idle = [&]( ) {
            LoaderDebugStats s;
            globalGameState.world.chunkLoader.debug_loader_stats( &s );
            return s.chunks_loading == 0 && s.terrain_queued == 0 && s.terrain_results == 0 && s.light_jobs == 0 && s.light_pending == 0 &&
                   s.light_dirty == 0 && s.light_bfs_active == 0 && s.light_seeds == 0 && s.light_stale_count == 0;
        };
        // REPGAME_LIGHT_VERIFY also checks once the post-flight hover has
        // drained (phase 2), so streaming-time light bugs get caught too.
        static int verify_hover_streak = 0;
        if ( run_policy().fly.phase == 2 && verify_hover_streak >= 0 && env_int( "REPGAME_LIGHT_VERIFY", 0 ) > 0 ) {
            verify_hover_streak = loader_idle( ) ? verify_hover_streak + 1 : 0;
            if ( verify_hover_streak >= ( run_policy().fly.settle_ticks > 0 ? run_policy().fly.settle_ticks : 1 ) ) {
                pr_debug( "Fly mode: hover drained at tick %ld", globalGameState.tick_number );
                globalGameState.world.chunkLoader.light_debug_verify( );
                verify_hover_streak = -1;
            }
        }
        if ( run_policy().fly.phase == 0 ) {
            bool go = true;
            if ( run_policy().fly.settle_ticks > 0 && globalGameState.tick_number < run_policy().fly.settle_timeout ) {
                const bool idle = loader_idle( );
                run_policy().fly.idle_streak = idle ? run_policy().fly.idle_streak + 1 : 0;
                go = run_policy().fly.idle_streak >= run_policy().fly.settle_ticks;
            }
            if ( go ) {
                run_policy().fly.phase = 1;
                run_policy().fly.fly_start_tick = globalGameState.tick_number;
                pr_debug( "Fly mode: settled, flight begins at tick %ld", run_policy().fly.fly_start_tick );
                // 2 = hover check only: the settle check freezes the main
                // loop for seconds, and the tick catch-up afterwards skews
                // the flight's timing metrics.
                if ( env_int( "REPGAME_LIGHT_VERIFY", 0 ) == 1 ) {
                    globalGameState.world.chunkLoader.light_debug_verify( );
                }
            }
        }
        // Absolute pin (not a velocity) so the flight path is exact and
        // repeatable; prev_* synced so draw() doesn't interpolate.
        double t = run_policy().fly.phase > 0 ? static_cast<double>( globalGameState.tick_number - run_policy().fly.fly_start_tick ) : 0.0;
        if ( run_policy().fly.fly_ticks >= 0 && t > run_policy().fly.fly_ticks ) {
            t = run_policy().fly.fly_ticks;
            run_policy().fly.phase = 2;
        }
        // Render distance schedule (REPGAME_FLY_RD): entries apply in order
        // once their fly tick is reached.
        while ( run_policy().fly.phase >= 1 && run_policy().fly.rd_next < run_policy().fly.rd_count && t >= run_policy().fly.rd_ticks[ run_policy().fly.rd_next ] ) {
            globalGameState.input.render_distance = run_policy().fly.rd_radius[ run_policy().fly.rd_next ];
            pr_debug( "Fly mode: render distance -> %d at fly tick %.0f", run_policy().fly.rd_radius[ run_policy().fly.rd_next ], t );
            run_policy().fly.rd_next++;
        }
        globalGameState.camera.pos = run_policy().fly.start + run_policy().fly.dir * ( run_policy().fly.speed * t );
        globalGameState.camera.prev_pos = globalGameState.camera.pos;
        globalGameState.camera.prev_eye_height = globalGameState.camera.eye_height;
        globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
        globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;
        if ( run_policy().fly.exit_tick && globalGameState.tick_number >= run_policy().fly.exit_tick ) {
            globalGameState.input.exitGame = true;
        }
    }

    if ( globalGameState.input.click_delay_right > 0 ) {
        globalGameState.input.click_delay_right--;
    } else {
        globalGameState.input.click_delay_right = 0;
    }
    if ( globalGameState.input.click_delay_left > 0 ) {
        globalGameState.input.click_delay_left--;
    } else {
        globalGameState.input.click_delay_left = 0;
    }
    if ( globalGameState.input.click_delay_middle > 0 ) {
        globalGameState.input.click_delay_middle--;
    } else {
        globalGameState.input.click_delay_middle = 0;
    }

    globalGameState.input.mouse.previousPosition.x = globalGameState.input.mouse.currentPosition.x;
    globalGameState.input.mouse.previousPosition.y = globalGameState.input.mouse.currentPosition.y;
    globalGameState.input.mouse.previousPosition.wheel_counts = globalGameState.input.mouse.currentPosition.wheel_counts;

    RepGame::process_inventory_events( );

    // Capture mode readiness: a view is "settled" when the terrain and light
    // queues have been idle for REPGAME_CAPTURE_SETTLE consecutive ticks —
    // everything visible is then generated, meshed, lit and uploaded.
    if ( run_policy().capture.enabled && !run_policy().capture.shot_pending && !run_policy().capture.finished ) {
        run_policy().capture.ticks_in_view++;
        LoaderDebugStats s;
        globalGameState.world.chunkLoader.debug_loader_stats( &s );
        const bool idle = s.chunks_loading == 0 && s.terrain_queued == 0 && s.terrain_results == 0 && //
                          s.light_jobs == 0 && s.light_pending == 0 && s.light_dirty == 0 &&         //
                          s.light_bfs_active == 0 && s.light_seeds == 0 && s.light_stale_count == 0;
        run_policy().capture.idle_streak = idle ? run_policy().capture.idle_streak + 1 : 0;
        // REPGAME_CAPTURE_VIEW_TICKS: views after the first shoot on a fixed
        // tick count regardless of queue state — the shot lands on the frame
        // the camera jumped to, catching transition transients (stale
        // culling/occlusion verdicts) that idle-settling would smooth over.
        const bool fixed_hold = run_policy().capture.view_index > 0 && run_policy().capture.view_ticks > 0;
        if ( fixed_hold ? run_policy().capture.ticks_in_view >= run_policy().capture.view_ticks
                        : run_policy().capture.idle_streak >= run_policy().capture.settle_ticks || run_policy().capture.ticks_in_view >= run_policy().capture.timeout_ticks ) {
            const bool timed_out = !fixed_hold && run_policy().capture.idle_streak < run_policy().capture.settle_ticks;
            pr_debug( "Capture view %d: shot pending after %ld ticks%s", run_policy().capture.view_index, run_policy().capture.ticks_in_view,
                      timed_out ? " (TIMEOUT — loader still busy)" : "" );
            run_policy().capture.shot_pending = true;
        }
    }

    // Fly-mode telemetry: one FLYMET line per REPGAME_FLY_METRICS ticks so the
    // light-queue dynamics can be plotted over the flight. Carries the same
    // counters as the LIGHT probe plus claim/window state (j=jobs p=pending
    // c=claims d=dirty x=dropped s=seeds; pri/starve = open priority windows).
    if ( run_policy().fly.enabled && run_policy().fly.metric_every > 0 && globalGameState.tick_number % run_policy().fly.metric_every == 0 ) {
        LoaderDebugStats s;
        globalGameState.world.chunkLoader.debug_loader_stats( &s );
        char dbg[ 1024 ];
        globalGameState.world.chunkLoader.light_dbg_stats( dbg, sizeof( dbg ) );
        pr_debug( "FLYMET t=%ld ph=%d rd=%d p=(%.1f,%.1f,%.1f) cl=%d j=%d p=%d c=%d d=%d x=%lld bfs=%d pri=%d stv=%d s=%lld stl=%d occ=%d/%d | %s",
                  globalGameState.tick_number, run_policy().fly.phase, globalGameState.input.render_distance, globalGameState.camera.pos.x, globalGameState.camera.pos.y,
                  globalGameState.camera.pos.z, s.chunks_loading, s.light_jobs, s.light_pending, s.light_claims, s.light_dirty,
                  s.light_dropped, s.light_bfs_active, s.light_bfs_priority, s.light_bfs_starve, s.light_seeds,
                  s.light_stale_count, s.chunks_occluded, s.chunks_drawable, dbg );
    }

    // Multiplayer test harness: ctl commands + JSONL dumps (no-op without
    // REPGAME_MP_DUMP).
    mp_test::tick( globalGameState );
}

// Whether either cell a standing player would occupy at (x, y, z) is solid
