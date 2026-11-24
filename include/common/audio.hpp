#pragma once

#include <cstdint>

// Maps a BlockID to the SFX pool name for breaking that block. Returns
// "dig_stone" for anything unrecognized. Compiled on every platform (pure
// id lookup, no audio state) so game code can call it unconditionally.
const char *audio_block_dig_pool( int block_id );
// Same for placing — identical to the dig pool except glass-type blocks,
// whose only pool is the break shatter ("glass"); those place like stone.
const char *audio_block_place_pool( int block_id );
// Same for footsteps on the block's top face ("step_stone" fallback; glass
// steps sound like stone — the pack has no glass step files).
const char *audio_block_step_pool( int block_id );

// Distance falloff shared by every positional SFX: inverse distance with a
// 4-block full-volume radius — gain 1.0 within 4, 0.5 at 8, 0.25 at 16,
// 0.125 at 32 — hard cut to silence at AUDIO_SFX_RANGE. Pure math,
// compiled on every platform so game code calls it unconditionally.
constexpr double AUDIO_SFX_RANGE = 64.0;
static inline float audio_distance_gain( const double dist ) {
    if ( dist >= AUDIO_SFX_RANGE ) {
        return 0.0f;
    }
    const double gain = 4.0 / dist;
    return gain < 1.0 ? static_cast<float>( gain ) : 1.0f;
}

constexpr float AUDIO_SFX_MIN_GAIN = 0.02f;      // plays quieter than this are skipped
constexpr int AUDIO_SFX_MAX_ACTIVE = 24;         // concurrent non-fading one-shots
constexpr uint64_t AUDIO_SFX_RETRIGGER_MS = 40;  // same-pool restart window

// Voice budget: given the play gains of the currently active (non-fading)
// plays, returns -1 = play (room left), -2 = drop the new play (it is no
// louder than the quietest active one), or i >= 0 = fade-stop active[i]
// (the quietest; first index wins ties) and play the new one.
static inline int audio_sfx_pick_victim( const float *active_gains, int n_active, float new_gain, int max_active ) {
    if ( n_active < max_active ) {
        return -1;
    }
    int quiet = 0;
    for ( int i = 1; i < n_active; i++ ) {
        if ( active_gains[ i ] < active_gains[ quiet ] ) {
            quiet = i;
        }
    }
    return new_gain > active_gains[ quiet ] ? quiet : -2;
}

// Same-pool retrigger throttle: true = drop the new play because the pool
// started within window_ms and the new play is not louder than that start.
static inline bool audio_sfx_throttled( uint64_t last_start_ms, float last_gain, uint64_t now_ms, float new_gain, uint64_t window_ms ) {
    if ( last_start_ms == 0 || now_ms < last_start_ms || now_ms - last_start_ms > window_ms ) {
        return false;
    }
    return new_gain <= last_gain;
}

// Embedded audio: OGG Vorbis (tools/sounds_manifest.txt) decoded by
// stb_vorbis and played through SDL audio — SDL3 device-bound streams on
// Linux/Windows/ARM/SteamFrame, a main-thread mix callback + WASMFS files on
// WASM (no blob embedding there). REPGAME_AUDIO is only defined by the build
// on platforms with audio; everywhere else these are no-op stubs so the
// game code doesn't need per-platform #ifdefs.
#if defined( REPGAME_AUDIO )

// Opens the default playback device and starts the music stream. Failure
// (no device, no driver) leaves audio disabled and the game silent.
void audio_init( );
// Per-tick housekeeping: schedules the next ambient track after a random
// gap and reaps finished one-shot SFX streams. `music_set` is the dimension
// music set (DIM_MUSIC_* in dimension_profile.hpp) the rotation draws from;
// when it changes (dimension hop) the in-flight track is cut and the new
// set starts after a short delay instead of the full random gap. `in_cave`
// is the caller's enclosed-space probe (skylight 0 at the head cell) and
// gates the cave-ambience mood sounds.
void audio_tick( long tick_number, int music_set, bool in_cave );
// Plays a random variant of the named SFX pool (manifest names dig_1..dig_n
// all belong to base "dig"). Unknown/empty pools are ignored.
void audio_play_sfx( const char *base_name );
// Same, scaled by an extra per-play gain (mob distance falloff).
// pitch_jitter > 0 randomizes the playback rate by ±jitter per play
// (mob chatter de-phasing; desktop only — WASM plays at the source rate).
void audio_play_sfx_gain( const char *base_name, float gain, float pitch_jitter = 0.0f );
// Same, but returns a nonzero handle while the play is in flight — for
// loop clips the caller may need to cut short (vehicle roll/inside).
// The handle goes stale when the clip ends; audio_sfx_stop on a stale
// handle is a no-op. 0 = audio off, play dropped by the SFX budget
// (AUDIO_SFX_* above), or the pool is unknown.
uint64_t audio_play_sfx_handle( const char *base_name, float gain, float pitch_jitter = 0.0f );
// Cuts an in-flight tracked play short with a ~50 ms fade (no click).
// 0/stale handle = no-op.
void audio_sfx_stop( uint64_t handle );
void audio_set_music_gain( float gain );
void audio_set_sfx_gain( float gain );
// Title of the track currently playing, or nullptr during gaps/silence.
const char *audio_current_track_title( );
void audio_cleanup( );

#else

static inline void audio_init( ) {
}
static inline void audio_tick( long tick_number, int music_set, bool in_cave ) {
}
static inline void audio_play_sfx( const char *base_name ) {
}
static inline void audio_play_sfx_gain( const char *base_name, float gain, float pitch_jitter = 0.0f ) {
}
static inline uint64_t audio_play_sfx_handle( const char *base_name, float gain, float pitch_jitter = 0.0f ) {
    return 0;
}
static inline void audio_sfx_stop( uint64_t handle ) {
}
static inline void audio_set_music_gain( float gain ) {
}
static inline void audio_set_sfx_gain( float gain ) {
}
static inline const char *audio_current_track_title( ) {
    return nullptr;
}
static inline void audio_cleanup( ) {
}

#endif
