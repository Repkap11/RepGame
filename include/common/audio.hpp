#pragma once

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
// gap and reaps finished one-shot SFX streams.
void audio_tick( long tick_number );
// Plays a random variant of the named SFX pool (manifest names dig_1..dig_n
// all belong to base "dig"). Unknown/empty pools are ignored.
void audio_play_sfx( const char *base_name );
// Same, scaled by an extra per-play gain (mob distance falloff).
void audio_play_sfx_gain( const char *base_name, float gain );
void audio_set_music_gain( float gain );
void audio_set_sfx_gain( float gain );
// Title of the track currently playing, or nullptr during gaps/silence.
const char *audio_current_track_title( );
void audio_cleanup( );

#else

static inline void audio_init( ) {
}
static inline void audio_tick( long tick_number ) {
}
static inline void audio_play_sfx( const char *base_name ) {
}
static inline void audio_play_sfx_gain( const char *base_name, float gain ) {
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
