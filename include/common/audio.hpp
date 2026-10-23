#pragma once

// Maps a BlockID to the SFX pool name for breaking/placing that block —
// Minecraft plays the block's dig sound for both. Returns "dig_stone" for
// anything unrecognized. Compiled on every platform (pure id lookup, no
// audio state) so game code can call it unconditionally.
const char *audio_block_dig_pool( int block_id );
// Same for footsteps on the block's top face ("step_stone" fallback; glass
// steps sound like stone — the pack has no glass step files).
const char *audio_block_step_pool( int block_id );

// Embedded audio: OGG Vorbis blobs (tools/sounds_manifest.txt) decoded by
// stb_vorbis and played through SDL3 audio streams. REPGAME_AUDIO is only
// defined by the build on platforms that embed blobs and use SDL3 (Linux,
// Windows, ARM, SteamFrame); everywhere else these are no-op stubs so the
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
