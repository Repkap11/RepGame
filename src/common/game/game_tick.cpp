// RepGame::tick — one simulation step, as an ordered list of named phases.
// Harness-only phases (capture/fly/walktest/script/results) live in
// game_harness.cpp; the VR phases live in vr.cpp. The call order below is
// the historical tick order — do not reorder.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/profile.hpp"
#include "common/world.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/angles.hpp"
#include "common/multiplayer.hpp"
#include "common/harness/run_policy.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/game/game_internal.hpp"

void RepGame::tick( ) {
    PROFILE_SCOPE( "tick" );
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    const bool vr_drives = vr_drives_camera( );
    if ( vr_drives ) {
        // VR controller state lands on the shared Input fields before anything
        // else reads them this tick.
        process_vr_input( );
        update_vr_ui_pointer( );
    }
    advance_clocks( );
    resolve_pending_spawn( );
    apply_harness_camera_pins( );
    // Snapshot the camera transform as it was at the end of the previous tick, so the
    // render loop can interpolate between this and the freshly-computed transform below.
    globalGameState.camera.snapshot_prev( );
    globalGameState.multiplayer.process_events( globalGameState );
    process_hotbar_scroll( );
    // Apply pending block updates before recomputing the selection so the
    // cursor reflects the post-destruction world. Player clicks queue events
    // tagged with the current tick, which processAllBlockUpdates defers until
    // the next tick (it skips events with tick_number >= current_tick); so
    // the destroy queued last tick is applied here, before the ray cast below.
    RepGame::process_block_updates( );
    close_out_of_reach_containers( );
    repgame_tick_sign_editor( globalGameState );
    if ( RepGame::should_lock_pointer( ) || run_policy().capture.enabled ) {
        if ( !run_policy().capture.enabled ) {
            RepGame::process_mouse_events( );
        }
        update_block_selection( );
        if ( !run_policy().capture.enabled ) {
            apply_mouse_look( );
        }
    } else {
        // Pointer unlocked (inventory open): abandon any in-progress mining.
        globalGameState.block_mining.progress_ticks = 0.0f;
    }
    update_break_progress( );
    recenter_mouse( );
    apply_harness_input_overrides( );
    if ( vr_drives ) {
        apply_vr_head_angles( );
    }
    // Refresh camera.movement (and the view matrices) from the current angle_H
    // before consuming it: process_movement builds the player's velocity from
    // camera.movement, so it must reflect this tick's angle_H rather than the
    // previous tick's. Running camera angle first keeps the walk direction in
    // sync with the look direction.
    RepGame::process_camera_angle( );
    RepGame::process_movement( );
    tick_vitals( );
    tick_entities( );
    // Furnaces burn and cook only in singleplayer; in multiplayer the server
    // ticks them authoritatively and streams CONTAINER_CONTENTS + lit-state
    // BLOCK_UPDATEs back.
    if ( !globalGameState.multiplayer.is_active( ) ) {
        globalGameState.world.tick_furnaces( );
    }
    // Tick-driven furnace changes (singleplayer) and inbound container
    // writes (multiplayer) converge here: icons follow the live contents —
    // the net layer never touches UI state directly.
    repgame_chest_sync_icons( globalGameState );
    repgame_furnace_sync_icons( globalGameState );
    RepGame::process_pressure_plates( );
    tick_fly_flight( );
    tick_input_latches( );
    RepGame::process_inventory_events( );
    tick_harness_reporting( );
}

void RepGame::advance_clocks( ) {
    globalGameState.tick_number++;
    // Music scheduling + one-shot SFX reaping (no-op without REPGAME_AUDIO).
    audio_tick( globalGameState.tick_number );
    // Feed the tick to the light pool: the BFS starve window measures backlog
    // age in ticks so the seed hot paths never read the clock.
    globalGameState.world.chunkLoader.light_note_tick( globalGameState.tick_number );
    // Day/night clock; light_daylight_factor() maps it to u_Daylight.
    globalGameState.world_time++;
}

void RepGame::resolve_pending_spawn( ) {
    repgame_resolve_pending_spawn( globalGameState );
}

// The body of RepGame::resolve_pending_spawn as a free function so rep_tests
// can drive it. Fresh spawn: the terrain-height estimate in
// initializeGameState can't see structure blocks (trees, features). Once
// every cell in the spawn column reports real generated data, stand on the
// topmost colliding block so the player never starts inside a trunk or
// spire. With spawn_point_set the target is the recorded bed instead:
// stand on the bed top, or fall back to the world-spawn column when the
// bed is missing or obstructed.
void repgame_resolve_pending_spawn( RepGameState &state ) {
    CameraState &cam = state.camera;
    if ( !state.spawn_pending ) {
        return;
    }
    if ( state.spawn_point_set ) {
        // Bed respawn: stand the player on the bed cell once its column
        // reports real data. A missing or obstructed bed clears the
        // point and falls through to the world-spawn scan.
        const glm::ivec3 bed = state.spawn_point;
        const BlockState bed_bs = state.world.get_loaded_block( bed );
        if ( bed_bs.id == LAST_BLOCK_ID ) {
            // The column isn't generated yet: keep the camera over it so
            // the chunk window centers there, and retry next tick.
            cam.pos.x = bed.x + 0.5;
            cam.pos.z = bed.z + 0.5;
            return;
        }
        if ( block_definition_get_definition( bed_bs.id )->is_bed ) {
            glm::dvec3 eye( bed.x + 0.5, Collision::block_collision_top( state.world, bed ) + PLAYER_EYE_HEIGHT, bed.z + 0.5 );
            for ( int tries = 0; tries < 4 && Collision::collides_at( state.world, eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ); tries++ ) {
                eye.y += 1.0;
            }
            if ( !Collision::collides_at( state.world, eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) ) {
                cam.pos = eye;
                cam.y_speed = 0.0;
                state.spawn_pending = false;
                return;
            }
        }
        // Bed gone or unreachable: forget the point and respawn at the
        // world-spawn column (camera re-aimed so that column loads).
        state.spawn_point_set = false;
        cam.pos = glm::dvec3( 0.5, ceil( MapGen::maxTerrainHeight( ) ) + 2.0, 0.5 );
    }
    const int top_bound = ( int )ceil( MapGen::maxTerrainHeight( ) );
    for ( int y = top_bound; y >= BEDROCK_LEVEL; y-- ) {
        const BlockState bs = state.world.get_loaded_block( glm::ivec3( 0, y, 0 ) );
        if ( bs.id == LAST_BLOCK_ID ) {
            break; // Some chunk in the column isn't generated yet — retry next tick.
        }
        if ( block_definition_get_definition( bs.id )->collides_with_player ) {
            cam.pos.x = 0.5;
            cam.pos.z = 0.5;
            cam.pos.y = y + 1.0 + PLAYER_EYE_HEIGHT + 0.5;
            cam.y_speed = 0.0;
            state.spawn_pending = false;
            break;
        }
    }
}

void RepGame::process_hotbar_scroll( ) {
    Input &in = globalGameState.input;
    const int wheel_diff = in.mouse.previousPosition.wheel_counts - in.mouse.currentPosition.wheel_counts;
    if ( wheel_diff != 0 && !in.menu_open ) {
        if ( in.inventory_open && globalGameState.game_mode == GameMode_Creative ) {
            globalGameState.ui.creative.incrementSelectedPage( wheel_diff );
        } else {
            ItemID holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
            globalGameState.ui.overlay.set_holding_item( holdingBlock );
        }
    }
}

void RepGame::close_out_of_reach_containers( ) {
    Input &in = globalGameState.input;
    // Close an open chest when the block is gone (broken by anyone), its
    // chunk unloaded, or the player walked out of reach.
    if ( globalGameState.ui.chest.open ) {
        const BlockState chest_state = globalGameState.world.get_loaded_block( globalGameState.ui.chest.pos );
        const glm::dvec3 chest_center = glm::dvec3( globalGameState.ui.chest.pos ) + glm::dvec3( 0.5 );
        const double dist = glm::length( globalGameState.camera.pos - chest_center );
        if ( !block_is_chest( chest_state.id ) || dist > REACH_DISTANCE + 2.0 ) {
            RepGame::close_chest( globalGameState );
            in.inventory_open = false;
        }
    }

    // Same for an open furnace: closes when the block is gone (broken by
    // anyone), its chunk unloaded, or the player walks out of reach.
    if ( globalGameState.ui.furnace.open ) {
        const BlockState furnace_state = globalGameState.world.get_loaded_block( globalGameState.ui.furnace.pos );
        const glm::dvec3 furnace_center = glm::dvec3( globalGameState.ui.furnace.pos ) + glm::dvec3( 0.5 );
        const double dist = glm::length( globalGameState.camera.pos - furnace_center );
        if ( !block_is_furnace( furnace_state.id ) || dist > REACH_DISTANCE + 2.0 ) {
            RepGame::close_furnace( globalGameState );
            in.inventory_open = false;
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
            in.inventory_open = false;
        }
    }
}

void RepGame::update_block_selection( ) {
    BlockSelection &sel = globalGameState.block_selection;
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
    sel.ray_origin = ray_origin;
    sel.ray_dir = glm::normalize( pos_with_reach - ray_origin );
    const ItemID selected_block = globalGameState.hotbar.getSelectedItem( );
    const bool pick_water = item_is_block( selected_block ) && block_definition_get_definition( static_cast<BlockID>( selected_block ) )->can_float_on_water;
    double hit_t = 0.0;
    sel.selectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, ray_origin, pos_with_reach, sel.pos_destroy, &whichFace, 0, &hit_t );

    sel.face = whichFace;
    sel.pos_create = RayTraversal::placement_cell( sel.pos_destroy, whichFace );
    // Exact hit position inside the cell — the stair half placement rule
    // reads the y fraction. t is along the normalized look direction.
    const glm::dvec3 hit_pos = ray_origin + sel.ray_dir * hit_t;
    sel.hit_y_in_cell = sel.selectionInBounds ? hit_pos.y - sel.pos_destroy.y : 0.0;

    if ( pick_water ) {
        int whichFace_water = 0;
        glm::ivec3 pos_destroy_water;
        sel.floatSelectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, ray_origin, pos_with_reach, pos_destroy_water, &whichFace_water, 1 );
        sel.pos_create_water = RayTraversal::placement_cell( pos_destroy_water, whichFace_water );
    } else {
        sel.floatSelectionInBounds = 0;
    }

    if ( item_is_bucket( selected_block ) ) {
        // Buckets pick fluids: pos_fluid is the fluid cell under the
        // crosshair (scoop target / pour-back target), pos_fluid_create the
        // cell beside the hit face a pour lands in. The empty bucket casts
        // the mode-3 ray — flowing cells are transparent so a fall or
        // shallow flow can't shadow the source behind them — while a
        // filled bucket keeps mode 2 so a pour can aim at the flow itself.
        int whichFace_fluid = 0;
        const int mode = item_bucket_fluid( selected_block ) == LAST_BLOCK_ID ? 3 : 2;
        sel.fluidSelectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, ray_origin, pos_with_reach, sel.pos_fluid, &whichFace_fluid, mode );
        sel.pos_fluid_create = RayTraversal::placement_cell( sel.pos_fluid, whichFace_fluid );
    } else {
        sel.fluidSelectionInBounds = 0;
    }

    // A mob under the crosshair beats the block behind/under it: clears
    // the block selection so the outline (and any place/pick) doesn't
    // reach through the mob.
    RepGame::resolve_target_mob( globalGameState, ray_origin );
    globalGameState.world.set_selected_block( sel.pos_destroy, sel.selectionInBounds );
}

void RepGame::apply_mouse_look( ) {
    Input &in = globalGameState.input;
    const float raw_dx = static_cast<float>( in.mouse.currentPosition.x - in.mouse.previousPosition.x );
    const float raw_dy = static_cast<float>( in.mouse.currentPosition.y - in.mouse.previousPosition.y );
    // Low-pass filter the per-tick look delta so that sub-pixel / slow-pan motion
    // (where integer mouse deltas alternate 0,1,0,1) becomes a steady fractional
    // velocity instead of a stuttered step. Adds a small amount of input latency.
    in.mouse.smoothed_dx = in.mouse.smoothed_dx * ( 1.0f - MOUSE_SMOOTHING ) + raw_dx * MOUSE_SMOOTHING;
    in.mouse.smoothed_dy = in.mouse.smoothed_dy * ( 1.0f - MOUSE_SMOOTHING ) + raw_dy * MOUSE_SMOOTHING;
    if ( globalGameState.vr.active ) {
        // VR: the mouse turns the body, not the head — horizontal
        // motion maps to body_yaw (the head's own orientation comes
        // from the HMD), vertical motion does nothing.
        globalGameState.vr.body_yaw = wrap_degrees_360( globalGameState.vr.body_yaw + in.mouse.smoothed_dx * MOUSE_SENSITIVITY );
    } else {
        globalGameState.camera.angle_H = wrap_degrees_360( globalGameState.camera.angle_H + in.mouse.smoothed_dx * MOUSE_SENSITIVITY );
        globalGameState.camera.angle_V += in.mouse.smoothed_dy * MOUSE_SENSITIVITY;
    }
}

void RepGame::update_break_progress( ) {
    // Feed mining progress to the selection overlay so the block shows crack
    // damage while the left button is held.
    float break_progress = 0.0f;
    if ( globalGameState.block_mining.progress_ticks > 0.0f ) {
        const float break_ticks = block_definition_get_definition( globalGameState.block_mining.id )->hardness * static_cast<float>( UPS_RATE );
        break_progress = break_ticks > 0.0f ? globalGameState.block_mining.progress_ticks / break_ticks : 0.0f;
    }
    globalGameState.world.mouseSelection.set_break_progress( break_progress );
}

void RepGame::recenter_mouse( ) {
    Input &in = globalGameState.input;
    in.mouse.currentPosition.x = globalGameState.screen.width / 2.0f;
    in.mouse.currentPosition.y = globalGameState.screen.height / 2.0f;
}

void RepGame::tick_entities( ) {
    RepGameState &gs = globalGameState;
    // Mob physics + wander/chase AI run on the same tick cadence as the
    // player. Frozen in capture mode and when the feet chunk isn't live.
    // The input marks the player targetable only when hostile mobs are
    // enabled and the player isn't no-clipping; the output carries melee
    // hits + creeper explosions back here.
    MobTickInput mob_in;
    mob_in.player_targetable = RepGame::player_targetable( gs );
    mob_in.world_time = gs.world_time;
    mob_in.player_eye_height = PLAYER_EYE_HEIGHT - ( gs.input.movement.sneakPressed ? PLAYER_SNEAK_EYE_DROP : 0.0f );
    // The held item tempts breedable livestock toward the player.
    mob_in.player_no_clip = gs.input.no_clip;
    mob_in.held_item = gs.hotbar.getSelectedItem( );
    // Owned hostiles also consider remote players (positions + targetable
    // flags come from the PLAYER_LOCATION stream).
    if ( gs.multiplayer.multiplayer_mode( ) ) {
        gs.world.multiplayer_avatars.fill_remote_targets( mob_in.remote_targets );
    }
    MobTickOutput mob_out;
    gs.world.mobs.tick( gs.world, gs.camera.pos, &mob_in, &mob_out );
    // A mob that landed on farmland tramples it back to dirt — the crop on
    // top pops through the neighbour-update kill path. Grazing sheep leave
    // the same grass→dirt conversion (their wool regrows on the finish).
    // Both are natural writes: a mob write must not flag the chunk
    // player_edited or every natural standing in it becomes "kept
    // livestock" under the pen rule.
    for ( const glm::ivec3 &pos : mob_out.tramples ) {
        repgame_convert_block_natural( gs, pos, DIRT );
    }
    for ( const glm::ivec3 &pos : mob_out.grazes ) {
        repgame_convert_block_natural( gs, pos, DIRT );
    }
    // Falling blocks + primed TNT bodies integrate on the same cadence.
    gs.world.bodies.tick( gs );
    // Dropped item entities: physics, merge, magnet/pickup, despawn.
    gs.world.items.tick( gs );
    // Arrow projectiles: ballistic flight, block sticking, mob hits.
    ArrowEntities::ArrowTickOutput arrow_out;
    gs.world.arrows.tick( gs, &arrow_out );
    // Arrow mob hits reuse the melee path: a local mob takes hurt + drops;
    // a remote-owned mob gets the ENTITY_ACTION forwarded to its owner.
    for ( const ArrowEntities::ArrowMobHit &hit : arrow_out.mob_hits ) {
        if ( gs.world.mobs.net_mirror_at( hit.mob_index ) ) {
            gs.world.mobs.mirror_hit_feedback( hit.mob_index, gs.camera.pos );
            NetEntityActionPayload act;
            act.id = gs.world.mobs.net_id_at( hit.mob_index );
            act.action = NET_ACTION_HIT;
            act.damage = hit.damage;
            act.ax = hit.pos.x;
            act.ay = hit.pos.y;
            act.az = hit.pos.z;
            act.attacker_survival = hit.attacker_survival;
            act.item = 0;
            gs.multiplayer.entities.send_entity_action( act );
        } else {
            const bool killed = gs.world.mobs.hurt( hit.mob_index, hit.damage, hit.pos, gs.camera.pos );
            if ( killed && hit.attacker_survival && !gs.world.mobs.is_baby_at( hit.mob_index ) ) {
                spawn_mob_drops( gs, gs.world.mobs.def_at( hit.mob_index ), gs.world.mobs.pos_at( hit.mob_index ) );
            }
        }
    }
    // Entity sync: apply pending role grants, advertise the VIEW, stream
    // ENTITY_STATE for owned entities. No-op in single player.
    gs.multiplayer.entities.tick( gs );
    for ( const MobPlayerHit &hit : mob_out.player_hits ) {
        if ( hit.target_player_id < 0 ) {
            RepGame::player_hurt( gs, hit.from, hit.damage );
        } else {
            // An owned hostile connected on a remote player — forward it.
            gs.multiplayer.entities.send_player_hit( hit.target_player_id, hit.from, hit.damage );
        }
    }
    for ( const glm::dvec3 &pos : mob_out.explosions ) {
        gs.blockUpdateQueue.addBlockUpdate<TntExplodeEvent>( gs.tick_number, glm::ivec3( glm::floor( pos + glm::dvec3( 0.0, 0.5, 0.0 ) ) ), 0, false );
    }
}

void RepGame::tick_input_latches( ) {
    Input &in = globalGameState.input;
    in.click_delay_right = in.click_delay_right > 0 ? in.click_delay_right - 1 : 0;
    in.click_delay_left = in.click_delay_left > 0 ? in.click_delay_left - 1 : 0;
    in.click_delay_middle = in.click_delay_middle > 0 ? in.click_delay_middle - 1 : 0;
    in.drop_item_delay = in.drop_item_delay > 0 ? in.drop_item_delay - 1 : 0;

    // The held-item swing follows any world-aimed left- or right-button
    // hold — mining, a mob hit, a block place, or a swing at air — and
    // keeps chopping until the button releases or a UI swallows the press.
    const bool left_swing = in.mouse.buttons.left && !in.mouse.buttons.left_pressed_in_ui;
    const bool right_swing = in.mouse.buttons.right && !in.mouse.buttons.right_pressed_in_ui;
    globalGameState.held_swing.tick( ( left_swing || right_swing ) && !in.inventory_open && !in.menu_open );

    in.mouse.previousPosition.x = in.mouse.currentPosition.x;
    in.mouse.previousPosition.y = in.mouse.currentPosition.y;
    in.mouse.previousPosition.wheel_counts = in.mouse.currentPosition.wheel_counts;
}
