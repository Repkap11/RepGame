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
}
