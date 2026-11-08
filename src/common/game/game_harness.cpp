// RepGame::tick harness phases — the scripted/debug paths (capture mode,
// fly mode, walktest/teleport, debug crash, results/metrics, mp_test,
// scenario scripts) split out of game_tick.cpp. Called in tick order.

#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/world.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/harness/results.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/harness/script.hpp"

// Shared idle predicate for the harness settle gates lives in
// dbgcmd::loader_idle (debug_commands.cpp) — the fly-mode settle gate,
// capture-mode readiness and script wait_idle all wait on it.
using dbgcmd::loader_idle;

// The shared flag block every scripted camera pin applies: fly with no-clip,
// zero vertical speed and no held movement input.
static void pin_harness_flight( Input &in, CameraState &cam ) {
    in.player_flying = true;
    in.no_clip = true;
    cam.y_speed = 0.0;
    in.movement.sizeH = 0.0f;
    in.movement.jumpPressed = false;
    in.movement.sneakPressed = false;
}

void RepGame::apply_harness_camera_pins( ) {
    RunPolicy &policy = run_policy( );
    Input &in = globalGameState.input;
    CameraState &cam = globalGameState.camera;
    // Capture mode: pin the simulation to the current view each tick, before
    // process_camera_angle/process_movement consume the camera/input state.
    if ( RepGame::capture_mode( ) ) {
        const CaptureView &view = policy.capture.views[ policy.capture.view_index ];
        globalGameState.world_time = policy.capture.world_time;
        in.worldDrawQuality = policy.capture.quality;
        cam.pos = glm::dvec3( view.x, view.y, view.z );
        cam.angle_H = view.yaw;
        cam.angle_V = view.pitch;
        pin_harness_flight( in, cam );
    }

    // Script mode: pin the camera to the script's `camera` view each tick —
    // same fields the capture view pin sets, so movement/VR can't drift it.
    if ( policy.script.pin_camera ) {
        const ScriptState &sc = policy.script;
        cam.pos = glm::dvec3( sc.cam_x, sc.cam_y, sc.cam_z );
        cam.angle_H = sc.cam_yaw;
        cam.angle_V = sc.cam_pitch;
        pin_harness_flight( in, cam );
    }
}

void RepGame::apply_harness_input_overrides( ) {
    RunPolicy &policy = run_policy( );
    Input &in = globalGameState.input;
    CameraState &cam = globalGameState.camera;
    if ( policy.walktest ) {
        // REPGAME_WALKTEST_TICKS=n: quit after n ticks (timeout/SIGTERM gets
        // swallowed by SDL, so cap the test length deterministically). Go
        // through the normal exit path so cleanup() joins the terrain
        // threads — exit() here tears down libamdhip64 while a worker may be
        // inside map_gen_load_block_hip, segfaulting during shutdown.
        if ( policy.walktest_ticks && globalGameState.tick_number >= policy.walktest_ticks ) {
            results::note_exit_reason( "walktest_ticks" );
            in.exitGame = true;
        }
        // REPGAME_TELEPORT="x,y,z,angle,sprint,spin_deg_per_tick[,pitch[,walk]]":
        // teleport on the first tick, then hold forward each tick (angle
        // optionally spins). walk=0 suppresses the held-forward input so the
        // camera pans in place instead of translating.
        const TeleportSpec &tp = policy.teleport;
        if ( tp.valid ) {
            if ( globalGameState.tick_number == 1 ) {
                cam.pos = tp.pos;
                cam.angle_V = tp.pitch;
                cam.y_speed = 0.0f;
                in.player_sprinting = tp.sprint;
            }
            cam.angle_H = tp.angle + tp.spin * globalGameState.tick_number;
        }
        // REPGAME_SCREENSHOT_TICK=n: request a framebuffer screenshot on tick n
        // (screenshots land in ./screenshots/).
        if ( policy.screenshot_tick > 0 && globalGameState.tick_number == policy.screenshot_tick ) {
            in.screenshot_requested = true;
        }
        in.movement.sizeH = static_cast<float>( tp.walk );
        in.movement.angleH = 0.0f;
        in.movement.jumpPressed = false;
        in.movement.sneakPressed = false;
    }
    // REPGAME_DEBUG_CRASH=segv|abort[@tick]: deliberate crash to exercise the
    // debug tooling (rep-dbg --gdb, sanitizer report collection).
    // `segv` with no @tick crashes on the first tick (>= comparison).
    if ( policy.debug_crash && globalGameState.tick_number >= policy.debug_crash_tick ) {
        if ( policy.debug_crash == 2 ) {
            abort( );
        }
        *reinterpret_cast<volatile int *>( static_cast<uintptr_t>( 0 ) ) = 0;
    }
    // Fly mode: scripted no-clip flight over fresh terrain (metrics-only —
    // the camera itself is scripted just like capture mode). Flags and view
    // angles go in here; the position pin lands right after process_movement
    // so physics can't fight it.
    if ( policy.fly.enabled && !policy.capture.enabled ) {
        cam.angle_H = policy.fly.yaw;
        cam.angle_V = policy.fly.pitch;
        pin_harness_flight( in, cam );
    }
}

void RepGame::tick_fly_flight( ) {
    RunPolicy &policy = run_policy( );
    FlyState &fly = policy.fly;
    if ( !( fly.enabled && !policy.capture.enabled ) ) {
        return;
    }
    CameraState &cam = globalGameState.camera;
    // Settle gate: hold at the start position until the loader and light
    // queues have been idle REPGAME_FLY_SETTLE consecutive ticks (or the
    // timeout expires) so the flight starts from a converged baseline.
    // REPGAME_LIGHT_VERIFY also checks once the post-flight hover has
    // drained (phase 2), so streaming-time light bugs get caught too.
    if ( fly.phase == 2 && fly.verify_hover_streak >= 0 && policy.light_verify > 0 ) {
        fly.verify_hover_streak = loader_idle( globalGameState.world.chunkLoader ) ? fly.verify_hover_streak + 1 : 0;
        if ( fly.verify_hover_streak >= ( fly.settle_ticks > 0 ? fly.settle_ticks : 1 ) ) {
            pr_debug( "Fly mode: hover drained at tick %ld", globalGameState.tick_number );
            globalGameState.world.chunkLoader.light_debug_verify( );
            fly.verify_hover_streak = -1;
        }
    }
    if ( fly.phase == 0 ) {
        bool go = true;
        if ( fly.settle_ticks > 0 && globalGameState.tick_number < fly.settle_timeout ) {
            const bool loader_is_idle = loader_idle( globalGameState.world.chunkLoader );
            fly.idle_streak = loader_is_idle ? fly.idle_streak + 1 : 0;
            go = fly.idle_streak >= fly.settle_ticks;
        }
        if ( go ) {
            fly.phase = 1;
            fly.fly_start_tick = globalGameState.tick_number;
            pr_debug( "Fly mode: settled, flight begins at tick %ld", fly.fly_start_tick );
            results::event( globalGameState.tick_number, "fly_settled" );
            // 2 = hover check only: the settle check freezes the main
            // loop for seconds, and the tick catch-up afterwards skews
            // the flight's timing metrics.
            if ( policy.light_verify == 1 ) {
                globalGameState.world.chunkLoader.light_debug_verify( );
            }
        }
    }
    // Absolute pin (not a velocity) so the flight path is exact and
    // repeatable; prev_* synced so draw() doesn't interpolate.
    double t = fly.phase > 0 ? static_cast<double>( globalGameState.tick_number - fly.fly_start_tick ) : 0.0;
    if ( fly.fly_ticks >= 0 && t > fly.fly_ticks ) {
        t = fly.fly_ticks;
        fly.phase = 2;
    }
    // Render distance schedule (REPGAME_FLY_RD): entries apply in order
    // once their fly tick is reached.
    while ( fly.phase >= 1 && fly.rd_next < fly.rd_count && t >= fly.rd_ticks[ fly.rd_next ] ) {
        globalGameState.input.render_distance = fly.rd_radius[ fly.rd_next ];
        pr_debug( "Fly mode: render distance -> %d at fly tick %.0f", fly.rd_radius[ fly.rd_next ], t );
        fly.rd_next++;
    }
    cam.pos = fly.start + fly.dir * ( fly.speed * t );
    cam.snapshot_prev( );
    if ( fly.exit_tick && globalGameState.tick_number >= fly.exit_tick ) {
        results::note_exit_reason( "fly_ticks" );
        globalGameState.input.exitGame = true;
    }
}

void RepGame::tick_harness_reporting( ) {
    RunPolicy &policy = run_policy( );
    // Capture mode readiness: a view is "settled" when the terrain and light
    // queues have been idle for REPGAME_CAPTURE_SETTLE consecutive ticks —
    // everything visible is then generated, meshed, lit and uploaded.
    if ( policy.capture.enabled && !policy.capture.shot_pending && !policy.capture.finished ) {
        policy.capture.ticks_in_view++;
        policy.capture.idle_streak = loader_idle( globalGameState.world.chunkLoader ) ? policy.capture.idle_streak + 1 : 0;
        // REPGAME_CAPTURE_VIEW_TICKS: views after the first shoot on a fixed
        // tick count regardless of queue state — the shot lands on the frame
        // the camera jumped to, catching transition transients (stale
        // culling/occlusion verdicts) that idle-settling would smooth over.
        const bool fixed_hold = policy.capture.view_index > 0 && policy.capture.view_ticks > 0;
        if ( fixed_hold ? policy.capture.ticks_in_view >= policy.capture.view_ticks
                        : policy.capture.idle_streak >= policy.capture.settle_ticks || policy.capture.ticks_in_view >= policy.capture.timeout_ticks ) {
            const bool timed_out = !fixed_hold && policy.capture.idle_streak < policy.capture.settle_ticks;
            pr_debug( "Capture view %d: shot pending after %ld ticks%s", policy.capture.view_index, policy.capture.ticks_in_view,
                      timed_out ? " (TIMEOUT — loader still busy)" : "" );
            policy.capture.shot_timed_out = timed_out;
            policy.capture.shot_pending = true;
        }
    }

    // Fly-mode telemetry: one FLYMET line per REPGAME_FLY_METRICS ticks so the
    // light-queue dynamics can be plotted over the flight. Carries the same
    // counters as the LIGHT probe plus claim/window state (j=jobs p=pending
    // c=claims d=dirty x=dropped s=seeds; pri/starve = open priority windows).
    const bool flymet_due = policy.fly.enabled && policy.fly.metric_every > 0 &&
                            globalGameState.tick_number % policy.fly.metric_every == 0;
    if ( flymet_due || results::metric_due( globalGameState.tick_number ) ) {
        // One probe per tick shared between FLYMET and the results metric
        // record: light_dbg_stats resets its interval counters, so a second
        // call would hand FLYMET a truncated sample.
        LoaderDebugStats s;
        globalGameState.world.chunkLoader.debug_loader_stats( &s );
        char dbg[ 1024 ];
        globalGameState.world.chunkLoader.light_dbg_stats( dbg, sizeof( dbg ) );
        if ( flymet_due ) {
            pr_debug( "FLYMET t=%ld ph=%d rd=%d p=(%.1f,%.1f,%.1f) cl=%d j=%d p=%d c=%d d=%d x=%lld bfs=%d pri=%d stv=%d s=%lld stl=%d occ=%d/%d | %s",
                      globalGameState.tick_number, policy.fly.phase, globalGameState.input.render_distance, globalGameState.camera.pos.x, globalGameState.camera.pos.y,
                      globalGameState.camera.pos.z, s.chunks_loading, s.light_jobs, s.light_pending, s.light_claims, s.light_dirty,
                      s.light_dropped, s.light_bfs_active, s.light_bfs_priority, s.light_bfs_starve, s.light_seeds,
                      s.light_stale_count, s.chunks_occluded, s.chunks_drawable, dbg );
        }
        if ( results::metric_due( globalGameState.tick_number ) ) {
            results::metric( globalGameState, s, dbg );
        }
    }

    // Multiplayer test harness: ctl commands + JSONL dumps (no-op without
    // REPGAME_MP_DUMP).
    mp_test::tick( globalGameState );

    // Scenario runner: REPGAME_SCRIPT command file, no-op when unset.
    script::tick( globalGameState, globalGameState.world.chunkLoader );
}
