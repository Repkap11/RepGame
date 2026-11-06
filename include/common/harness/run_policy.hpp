#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "common/world.hpp" // WorldDrawQuality, DAY_LENGTH_TICKS, UPS_RATE

// Env-driven run policy: every REPGAME_* harness/debug switch that steers the
// simulation without touching gameplay defaults, parsed once on first access.
// run_policy() is cheap to call — the parse is a one-shot static init — but
// hot paths may cache the reference. When the corresponding env var is unset
// the matching check/hook is a complete no-op.

// --- Capture mode (REPGAME_CAPTURE) -------------------------------------------
// REPGAME_CAPTURE="x,y,z,yaw,pitch[,flags];...": deterministic screenshots of
// exact camera views for visual debugging/A-B tests. tick() pins the camera,
// clock and draw quality to the current view; once the loader/light queues are
// idle for REPGAME_CAPTURE_SETTLE ticks (or REPGAME_CAPTURE_TIMEOUT hits), the
// next draw() shoots screenshots/capture_<i>_*.tga and advances.
// REPGAME_CAPTURE_VIEW_TICKS=n: views after the first shoot exactly n ticks
// after the camera jumps to them instead of waiting for idle — a same-position
// yaw sweep at n=1 captures the transient frames of a fast mouse pan that a
// settled shot would miss.
struct CaptureView {
    double x, y, z;
    float yaw, pitch;
    int flags;
};
struct CaptureState {
    bool enabled = false;
    std::vector<CaptureView> views;
    int view_index = 0;
    int applied_view = -1; // last view whose prev_* camera snapshot was synced
    long ticks_in_view = 0;
    long idle_streak = 0;
    bool shot_pending = false;
    bool shot_timed_out = false; // the pending shot hit REPGAME_CAPTURE_TIMEOUT
    // WASM only: all views shot — stay on the last view instead of exiting
    // (exit would reload the page and drop the captures).
    bool finished = false;
    long world_time = DAY_LENGTH_TICKS / 4;
    WorldDrawQuality quality = WorldDrawQuality::MEDIUM;
    long settle_ticks = 60;
    long timeout_ticks = 30 * UPS_RATE;
    long view_ticks = -1; // fixed hold for views >0; <0 = idle-settle like view 0
};

// --- Fly mode (REPGAME_FLY) ---------------------------------------------------
// REPGAME_FLY="x,y,z,yaw,pitch[,speed]": scripted no-clip flight for load-in
// stress tests. tick() pins the camera to start + look*speed*t each tick, so
// a flight over fresh terrain is reproducible; FLYMET metrics lines carrying
// the light/loader counters are emitted every REPGAME_FLY_METRICS ticks
// (default 1). REPGAME_FLY_TICKS=n exits at tick n. player.dat is not written.
struct FlyState {
    bool enabled = false;
    glm::dvec3 start{ 0 };
    glm::dvec3 dir{ 0 };
    float yaw = 0, pitch = 0;
    double speed = 8.0;
    long metric_every = 1;
    long exit_tick = 0;
    long settle_ticks = 0;   // consecutive idle ticks required before flying (0 = fly immediately)
    long settle_timeout = 0; // give up waiting after this many ticks
    long fly_ticks = -1;     // flight duration; -1 = fly until exit_tick
    long fly_start_tick = 0;
    int idle_streak = 0;
    int phase = 0; // 0 = waiting for settle, 1 = flying, 2 = done (position held)
    // REPGAME_FLY_RD="tick:radius,tick:radius" — render distance changes
    // applied at the given fly ticks (t = ticks since flight start).
    static constexpr int RD_SCHEDULE_MAX = 16;
    long rd_ticks[ RD_SCHEDULE_MAX ] = { 0 };
    int rd_radius[ RD_SCHEDULE_MAX ] = { 0 };
    int rd_count = 0;
    int rd_next = 0;
};

// --- Script mode (REPGAME_SCRIPT) --------------------------------------------
// REPGAME_SCRIPT=<file>: a line-per-command scenario file executed by
// src/common/harness/script.cpp each tick — waits and asserts go through the
// results stream. REPGAME_SCRIPT_KEEP=1 keeps the game running (camera
// released) when the script ends instead of exiting. pin_camera/cam_*/shot_*
// are driven by script.cpp and consumed in game_tick/game_render.
struct ScriptState {
    bool enabled = false;
    const char *path = nullptr;
    bool keep = false;
    bool pin_camera = false;
    double cam_x = 0, cam_y = 0, cam_z = 0;
    float cam_yaw = 0, cam_pitch = 0;
    bool shot_pending = false;
    std::string shot_name;
};

struct RunPolicy {
    CaptureState capture;
    FlyState fly;
    ScriptState script;

    // REPGAME_WALKTEST: scripted walk — holds forward, logs a WT line per
    // tick, teleports via REPGAME_TELEPORT="x,y,z,angle,sprint,spin[,pitch]".
    bool walktest = false;
    // REPGAME_WALKTEST_TICKS=n: quit after n ticks.
    long walktest_ticks = 0;
    // REPGAME_TELEPORT raw spec; parsed per tick at the use site.
    const char *teleport = nullptr;
    // REPGAME_SCREENSHOT_TICK=n: framebuffer screenshot on tick n.
    long screenshot_tick = 0;
    // REPGAME_CAPTURE_PERIODIC=n: FBO+display dump every n frames.
    int capture_periodic = 0;
    // REPGAME_CAPTURE_ANOMALY: 1 = display outlier scan, 2 = +FBO alpha scan.
    int capture_anomaly = 0;

    // REPGAME_DEBUG_CRASH=segv|abort[@tick]: deliberate crash at that tick
    // (default 0) — a debug-tools self-test hook for rep-dbg --gdb and the
    // sanitizer builds. 0=off, 1=segv (null write), 2=abort.
    int debug_crash = 0;
    long debug_crash_tick = 0;

    // REPGAME_RESULTS=<path>: structured JSONL event stream (header, metric,
    // event, probe, assert, summary records — see docs/debug-tools.md);
    // presence enables the results writer.
    const char *results_path = nullptr;
    // REPGAME_RESULTS_EVERY=n: metric record cadence in ticks (default 20).
    long results_every = 20;
    // REPGAME_GIT=<sha>: stamped into the header record (rep-dbg sets it).
    const char *git_sha = nullptr;
    // REPGAME_PROFILE=1: PROFILE_SCOPE timers aggregate into each metric
    // record's "prof" object (docs/debug-tools.md).
    bool profile = false;

    // True while a scripted harness owns the camera — VR input, pointer lock
    // and HUD interaction are suppressed on these paths.
    bool camera_pinned( ) const {
        return capture.enabled || fly.enabled || script.pin_camera;
    }
};

// Lazily parsed once; the returned reference stays valid for the process.
RunPolicy &run_policy( );
