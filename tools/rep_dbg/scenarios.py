"""Named scenario shortcuts for `rep-dbg ab` — env-pair dicts encoding the
popin experiments (was /tmp/popin/run.sh). Adding a scenario is a one-line
entry here."""

SCENARIOS = {
    # Fly parked at a fixed spot; the flight only begins after a long idle
    # settle. The interesting metric is game.settle_tick.
    "cold-settle": {
        "REPGAME_FLY": "1000,80,1000,0,0,2",
        "REPGAME_FLY_SETTLE": "20",
        "REPGAME_FLY_SETTLE_TIMEOUT": "6000",
        "REPGAME_FLY_METRICS": "20",
        "REPGAME_FLY_TICKS": "6000",
    },
    # Same settle, then a 600-tick streaming flight and a 300-tick hover.
    "stream-flight": {
        "REPGAME_FLY": "1000,80,1000,0,0,2",
        "REPGAME_FLY_SETTLE": "20",
        "REPGAME_FLY_SETTLE_TIMEOUT": "6000",
        "REPGAME_FLY_METRICS": "20",
        "REPGAME_FLY_FLY_TICKS": "600",
        "REPGAME_FLY_TICKS": "900",
    },
    # Parked flat-world idle: settles, then rep-dbg holds a timed window.
    # No FLY_TICKS — the runner stops the game after the window.
    "idle": {
        "REPGAME_FLY": "1000,80,1000,0,0,0",
        "REPGAME_FLY_SETTLE": "20",
        "REPGAME_FLY_SETTLE_TIMEOUT": "6000",
        "REPGAME_FLY_METRICS": "20",
        "REPGAME_PROFILE": "1",
    },
    # Mob stress world (REPGAME_MOB_WORLD, docs/mobs.md): a walled arena of
    # N mobs, camera parked behind the +z wall looking over the crowd.
    # Override the population per cfg, e.g.
    # --cfg n2000=current:REPGAME_MOB_WORLD=2000 or ...=2000,ai=0,pig.
    "mob-stress": {
        "REPGAME_MOB_WORLD": "500",
        "REPGAME_FLY": "0.5,30,60,0,30,0",
        "REPGAME_FLY_SETTLE": "20",
        "REPGAME_FLY_SETTLE_TIMEOUT": "6000",
        "REPGAME_FLY_METRICS": "20",
        "REPGAME_PROFILE": "1",
    },
}

# Scenarios that run under a fixed-duration window (runner opts.window):
# reps measure the window only; wall_s is fixed by rep-dbg, so the
# interesting numbers are window.fps and window.prof.*.us_per_call.
WINDOWS = {
    "idle": {"after_event": "fly_settled", "warmup_s": 2, "window_s": 10},
    "mob-stress": {"after_event": "fly_settled", "warmup_s": 2, "window_s": 10},
}

WINDOW_METRICS = [
    "window.fps",
    "window.prof.draw_frame.us_per_call",
    "window.prof.tick.us_per_call",
]

# Per-scenario overrides of the default window metric set.
SCENARIO_WINDOW_METRICS = {
    "mob-stress": WINDOW_METRICS + [
        "window.prof.mobs_tick.us_per_call",
        "window.prof.mobs_crowding.us_per_call",
        "window.prof.mobs_render.us_per_call",
        "window.prof.mobs_draw.us_per_call",
        "window.prof.world_draw.us_per_call",
        "window.prof.arrows_tick.us_per_call",
        "stats.metric.mobs.total.mean",
        "stats.metric.mobs.frozen.mean",
    ],
}


def window_metrics(scenario):
    """The default `ab` metric list for a windowed scenario."""
    return SCENARIO_WINDOW_METRICS.get(scenario, WINDOW_METRICS)
