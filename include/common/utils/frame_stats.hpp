#pragma once

// Rolling frame-time stats: avg / 1% low / 0.1% low FPS, each over its own
// trailing time window so a single hitch ages out of the shorter lows
// quickly instead of lingering. "X% low" = average FPS of the slowest X%
// of frames in that metric's window (count / sum of their durations, at
// least one frame) — the GamersNexus/CapFrameX "low average" definition,
// not a percentile. The 5s 0.1% window keeps ~1 slow frame per 1000
// meaningful at ~200fps; 1% only needs ~2s for the same density.

#include <vector>

class FrameTimeStats {
  public:
    static constexpr float AVG_WINDOW_S = 1.0f;                 // trailing window for avg fps / ms
    static constexpr float LOW1_WINDOW_S = 2.0f;                // trailing window for the 1% low
    static constexpr float LOW01_WINDOW_S = 5.0f;               // trailing window for the 0.1% low
    static constexpr float RETAIN_S = LOW01_WINDOW_S;           // ring retention (the longest window)
    static constexpr int MAX_SAMPLES = 16384;                   // ring capacity (cap at very high fps)
    static constexpr float RECOMPUTE_S = 0.25f;                 // summary refresh period

    struct Summary {
        float avg_fps = 0;
        float low1_fps = 0;
        float low01_fps = 0;
        float avg_ms = 0;
        int frames = 0;
    };

    // Ignores dt <= 0; keeps the smallest suffix of samples covering
    // >= RETAIN_S (plus the newest frame) and caps the ring at
    // MAX_SAMPLES; refreshes the cached summary every RECOMPUTE_S of
    // accumulated frame time (and on the first frame).
    void add_frame( float dt_s );

    const Summary &summary( ) const {
        return summary_;
    }

    // Full computation over the retained samples (used by add_frame's
    // refresh, and by tests).
    Summary compute( ) const;

  private:
    // Count of newest samples whose durations sum to >= window_s
    // (all retained samples if the ring holds less).
    int suffix_len( float window_s ) const;

    std::vector<float> samples_ = std::vector<float>( MAX_SAMPLES );
    int head_ = 0;
    int count_ = 0;
    double window_sum_ = 0;
    float since_refresh_ = 0;
    Summary summary_;
};
