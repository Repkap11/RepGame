#include "common/audio.hpp"

#if defined( REPGAME_AUDIO )

#include <atomic>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "common/RepGame.hpp" // capture_mode
#include "common/RepGameSDL2.hpp" // SDL3 headers (SDL2 on WASM, which never defines REPGAME_AUDIO)
#include "common/Logging.hpp"
#include "common/constants.hpp"
// Decls only — the impl lives in vendor/stb_vorbis.cpp. The NO_* defines
// must match it so declarations and definitions agree.
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtautological-compare"
#endif
#include "common/vendor/stb_vorbis.hpp"
#if defined( __GNUC__ ) || defined( __clang__ )
#pragma GCC diagnostic pop
#endif
// Generated at build time into out/include (see tools/convert_sounds.sh gen):
// MK_BLOB decls per manifest entry + MUSIC_TRACKS[] / SFX_SOUNDS[] tables.
#include "sounds_manifest.hpp"

// Music is streamed: the persistent music stream's get-callback (audio
// thread) pulls decoded float frames out of stb_vorbis. One-shot SFX are
// fully decoded once, then each play pushes the whole PCM into a fresh
// stream bound to the device — SDL3 mixes all bound streams itself.
namespace {

SDL_AudioDeviceID audio_device = 0;
SDL_AudioSpec device_spec;
SDL_AudioStream *music_stream = nullptr;
stb_vorbis *music_vorbis = nullptr; // audio thread owns this

bool audio_enabled = false;
bool music_enabled = true;
float sfx_gain = 1.0f;

// Track scheduling: the audio thread sets music_want when it runs out of
// track; the game thread (audio_tick) picks the next index and hands it over
// through next_track.
std::atomic<int> next_track{ -1 };
std::atomic<bool> music_want{ false };
std::atomic<int> current_track{ -1 }; // audio thread writes, game thread reads
int last_track = -1;
long gap_until_tick = 0;
long gap_min_ticks = 0;
long gap_span_ticks = 0;

struct SfxPcm {
    float *data = nullptr;
    int frames = 0;
    int channels = 0;
    int rate = 0;
    bool decoded = false;
};
std::vector<SfxPcm> sfx_cache;
std::vector<SDL_AudioStream *> active_sfx;

float env_float( const char *name, float fallback ) {
    const char *env = getenv( name );
    if ( !env ) {
        return fallback;
    }
    const float v = atof( env );
    return v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
}

// audio thread: open the next queued blob as the current vorbis stream.
static void music_open_track( int idx ) {
    const MusicTrackEntry *track = &MUSIC_TRACKS[ idx ];
    int err = 0;
    music_vorbis = stb_vorbis_open_memory( ( const unsigned char * )track->blob->source, track->blob->length, &err, NULL );
    if ( !music_vorbis ) {
        pr_debug( "audio: vorbis open failed for '%s' err=%d", track->title, err );
        music_want.store( true );
        return;
    }
    const stb_vorbis_info info = stb_vorbis_get_info( music_vorbis );
    const SDL_AudioSpec src = { SDL_AUDIO_F32, ( Uint8 )info.channels, ( int )info.sample_rate };
    SDL_SetAudioStreamFormat( music_stream, &src, &device_spec );
    last_track = idx;
    current_track.store( idx );
    pr_debug( "audio: now playing %s", track->title );
}

// audio thread: feed the music stream when the device drains it.
static void SDLCALL music_feed( void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount ) {
    static float buf[ 4096 * 2 ];
    int floats_wanted = additional_amount / ( int )sizeof( float );
    while ( floats_wanted > 0 ) {
        if ( !music_vorbis ) {
            const int idx = next_track.exchange( -1 );
            if ( idx < 0 ) {
                music_want.store( true );
                break;
            }
            music_open_track( idx );
            if ( !music_vorbis ) {
                break;
            }
        }
        const stb_vorbis_info info = stb_vorbis_get_info( music_vorbis );
        int want = floats_wanted - ( floats_wanted % info.channels );
        if ( want <= 0 ) {
            break;
        }
        const int buf_floats = ( int )( sizeof( buf ) / sizeof( float ) );
        if ( want > buf_floats ) {
            want = buf_floats;
        }
        const int got = stb_vorbis_get_samples_float_interleaved( music_vorbis, info.channels, buf, want );
        if ( got <= 0 ) {
            stb_vorbis_close( music_vorbis );
            music_vorbis = nullptr;
            current_track.store( -1 );
            continue;
        }
        SDL_PutAudioStreamData( stream, buf, got * info.channels * ( int )sizeof( float ) );
        floats_wanted -= got * info.channels;
    }
}

// game thread: decode a whole SFX blob to float PCM (SFX are short).
static bool sfx_decode( int idx, SfxPcm *out ) {
    const BinaryBlob *blob = SFX_SOUNDS[ idx ].blob;
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory( ( const unsigned char * )blob->source, blob->length, &err, NULL );
    if ( !v ) {
        pr_debug( "audio: vorbis open failed for sfx '%s' err=%d", SFX_SOUNDS[ idx ].base, err );
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info( v );
    const int total_frames = ( int )stb_vorbis_stream_length_in_samples( v );
    out->data = ( float * )malloc( total_frames * info.channels * sizeof( float ) );
    out->frames = stb_vorbis_get_samples_float_interleaved( v, info.channels, out->data, total_frames * info.channels );
    out->channels = info.channels;
    out->rate = info.sample_rate;
    out->decoded = true;
    stb_vorbis_close( v );
    return out->frames > 0;
}

} // namespace

void audio_init( ) {
    const char *env_audio = getenv( "REPGAME_AUDIO" );
    if ( ( env_audio && atoi( env_audio ) == 0 ) || RepGame::capture_mode( ) ) {
        return;
    }
    srand( ( unsigned )time( NULL ) );
    const char *env_music = getenv( "REPGAME_MUSIC" );
    if ( env_music && atoi( env_music ) == 0 ) {
        music_enabled = false;
    }
    const float music_gain = env_float( "REPGAME_MUSIC_VOLUME", 0.5f );
    sfx_gain = env_float( "REPGAME_SFX_VOLUME", 1.0f );
    // Seconds of silence between tracks, uniform in [gap/4, gap].
    // REPGAME_MUSIC_GAP=0 = continuous shuffle.
    const char *env_gap = getenv( "REPGAME_MUSIC_GAP" );
    const float gap_s = env_gap ? atof( env_gap ) : 240.0f;
    gap_min_ticks = ( long )( gap_s * 0.25f * UPS_RATE );
    gap_span_ticks = ( long )( gap_s * 0.75f * UPS_RATE ) + 1;

    const SDL_AudioSpec want = { SDL_AUDIO_F32, 2, 48000 };
    music_stream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, music_feed, NULL );
    if ( !music_stream ) {
        pr_debug( "audio: no playback device (%s), running silent", SDL_GetError( ) );
        return;
    }
    audio_device = SDL_GetAudioStreamDevice( music_stream );
    SDL_GetAudioDeviceFormat( audio_device, &device_spec, NULL );
    SDL_SetAudioStreamGain( music_stream, music_gain );

    const int num_sfx = ( int )( sizeof( SFX_SOUNDS ) / sizeof( SFX_SOUNDS[ 0 ] ) ) - 1; // NULL terminator row
    sfx_cache.resize( num_sfx > 0 ? num_sfx : 0 );

    audio_enabled = true;
    music_want.store( true );
    SDL_ResumeAudioStreamDevice( music_stream );
    pr_debug( "audio: device %dHz %dch, %d music tracks, %d sfx", device_spec.freq, device_spec.channels,
              ( int )( sizeof( MUSIC_TRACKS ) / sizeof( MUSIC_TRACKS[ 0 ] ) ) - 1, num_sfx );
}

void audio_tick( long tick_number ) {
    if ( !audio_enabled ) {
        return;
    }
    const int num_music = ( int )( sizeof( MUSIC_TRACKS ) / sizeof( MUSIC_TRACKS[ 0 ] ) ) - 1;
    if ( music_enabled && num_music > 0 && music_want.load( ) && next_track.load( ) < 0 ) {
        if ( gap_until_tick == 0 ) {
            // The first track starts after a few seconds (world still loading);
            // later tracks wait out the full random gap.
            const long delay = last_track < 0 ? 3 * UPS_RATE
                                              : gap_min_ticks + ( gap_span_ticks > 0 ? rand( ) % gap_span_ticks : 0 );
            gap_until_tick = tick_number + delay;
        }
        if ( tick_number >= gap_until_tick ) {
            int idx = rand( ) % num_music;
            if ( num_music > 1 && idx == last_track ) {
                idx = ( idx + 1 ) % num_music;
            }
            gap_until_tick = 0;
            music_want.store( false );
            next_track.store( idx );
        }
    }
    // Reap finished one-shots: once a flushed stream has no queued input and
    // no converted output left, the device already consumed every sample.
    for ( size_t i = 0; i < active_sfx.size( ); ) {
        if ( SDL_GetAudioStreamQueued( active_sfx[ i ] ) == 0 && SDL_GetAudioStreamAvailable( active_sfx[ i ] ) == 0 ) {
            SDL_UnbindAudioStream( active_sfx[ i ] );
            SDL_DestroyAudioStream( active_sfx[ i ] );
            active_sfx[ i ] = active_sfx.back( );
            active_sfx.pop_back( );
        } else {
            i++;
        }
    }
}

void audio_play_sfx( const char *base_name ) {
    if ( !audio_enabled ) {
        return;
    }
    int variants[ 16 ];
    int num_variants = 0;
    const int num_sfx = ( int )sfx_cache.size( );
    for ( int i = 0; i < num_sfx && num_variants < 16; i++ ) {
        if ( strcmp( SFX_SOUNDS[ i ].base, base_name ) == 0 ) {
            variants[ num_variants++ ] = i;
        }
    }
    if ( num_variants == 0 ) {
        return;
    }
    const int idx = variants[ rand( ) % num_variants ];
    SfxPcm &pcm = sfx_cache[ idx ];
    if ( !pcm.decoded && !sfx_decode( idx, &pcm ) ) {
        return;
    }
    const SDL_AudioSpec src = { SDL_AUDIO_F32, ( Uint8 )pcm.channels, pcm.rate };
    SDL_AudioStream *s = SDL_CreateAudioStream( &src, &device_spec );
    if ( !s ) {
        return;
    }
    SDL_SetAudioStreamGain( s, sfx_gain );
    SDL_PutAudioStreamData( s, pcm.data, pcm.frames * pcm.channels * ( int )sizeof( float ) );
    SDL_FlushAudioStream( s );
    SDL_BindAudioStream( audio_device, s );
    active_sfx.push_back( s );
}

void audio_set_music_gain( float gain ) {
    if ( music_stream ) {
        SDL_SetAudioStreamGain( music_stream, gain );
    }
}

void audio_set_sfx_gain( float gain ) {
    sfx_gain = gain;
}

const char *audio_current_track_title( ) {
    const int idx = current_track.load( );
    return idx < 0 ? nullptr : MUSIC_TRACKS[ idx ].title;
}

void audio_cleanup( ) {
    for ( SDL_AudioStream *s : active_sfx ) {
        SDL_UnbindAudioStream( s );
        SDL_DestroyAudioStream( s );
    }
    active_sfx.clear( );
    // Destroying the stream SDL_OpenAudioDeviceStream returned also closes
    // the device it opened.
    if ( music_stream ) {
        SDL_DestroyAudioStream( music_stream );
        music_stream = nullptr;
    }
    for ( SfxPcm &pcm : sfx_cache ) {
        free( pcm.data );
    }
    sfx_cache.clear( );
    current_track.store( -1 );
    audio_enabled = false;
}

#endif
