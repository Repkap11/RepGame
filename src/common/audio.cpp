#include "common/audio.hpp"
#include "common/block_definitions.hpp"

#include <stdio.h>
#include <string.h>

// Per-material dig pools (see tools/sounds_manifest.txt). Grouped by the
// Minecraft sound type of each block; default is stone, which also covers
// metal blocks, ores, furnaces, pistons and redstone components.
static const char *block_material( int block_id ) {
    switch ( block_id ) {
    // Soil + vegetation (leaves, flowers, crops, TNT) -> grass
    case GRASS:
    case GRASS_SIDE:
    case SNOWY_GRASS:
    case MYCELIUM:
    case MYCELIUM_SIDE:
    case PODZEL:
    case PODZEL_SIDE:
    case TNT:
    case TNT_TOP:
    case TNT_BOTTOM:
    case SPIDER_WEB:
    case FIRE:
    case LEAF:
    case BIRTCH_LEAVES:
    case PINE_LEAF:
    case JUNGLE_LEAF:
    case RED_FLOWER:
    case YELLOW_FLOWER:
    case BLUE_FLOWER:
    case POPPY_FLOWER:
    case LARGE_BLUE_FLOWER:
    case LARGE_PURPLE_FLOWER:
    case LARGE_WHITE_FLOWER:
    case RED_TULIP:
    case ORANGE_TULIP:
    case WHITE_TULIP:
    case PINK_TULIP:
    case SMALL_SUNFLOWER:
    case LARGE_SUNFLOWER:
    case SMALL_LILLIC_FLOWER:
    case SAPPLING:
    case JUNGLE_SAPLING:
    case BIRTCH_SAPPLING:
    case DARK_OAK_SAPPLING:
    case FAT_OAK_SAPPLING:
    case ACACIA_SAPPLING:
    case LIGHT_DEAD_SAPPLING:
    case DARK_DEAD_SAPPLING:
    case GARBAGE_SAPPLING:
    case BIRTCH_SAPLING_IN_POT:
    case OAK_SAPPLING_IN_POT:
    case LIGHT_DEAD_SAPPLING_IN_POT:
    case PINE_SAPPLING_IN_POT:
    case DARK_OAK_SAPPLING_IN_POT:
    case WHITE_FLOWER_SAPPLING_IN_POT:
    case RED_FLOWER_IN_POT:
    case GRASS_TUFT:
    case GRASS_TUFT2:
    case GRASS_TUFT3:
    case GRASS_TUFT4:
    case WHEAT_0:
    case WHEAT_1:
    case WHEAT_2:
    case WHEAT_3:
    case WHEAT_4:
    case WHEAT_5:
    case WHEAT_6:
    case WHEAT_7:
    case CARROT_0:
    case CARROT_1:
    case CARROT_2:
    case CARROT_3:
    case CARROT_4:
    case REED:
    case LILLYPAD:
    case MELLON_STEM:
    case NETHER_WART_1:
    case NETHER_WART_2:
    case NETHER_WART_3:
    case CHORUS_0:
    case CHORUS_1:
    case CHORUS_2:
    case RED_MUSHROOM:
    case BROWN_MUSHROOM:
    case RED_MUSHROOM_IN_POT:
        return "grass";
    case DIRT:
    case GRAVEL:
        return "gravel";
    case SAND:
    case ORANGE_SAND:
        return "sand";
    case SNOW:
        return "snow";
    // Logs, planks, wooden parts, chests, work stations, torches
    case OAK_PLANK:
    case OAK_LOG:
    case OAK_LOG_TOP:
    case OAK_SLAB:
    case DOUBLE_SLAB:
    case SLAB_TOP:
    case PINE_PLANK:
    case BIRTCH_PLANK:
    case JUNGLE_PLANK:
    case ACACIA_PLANK:
    case DARK_OAK_PLANK:
    case BIRTCH_LOG:
    case BIRTCH_LOG_TOP:
    case JUNGLE_LOG:
    case JUNGLE_LOG_TOP:
    case ACACIA_LOG:
    case ACACIA_LOG_TOP:
    case DARK_OAK_LOG:
    case DARK_OAK_LOG_TOP:
    case PINE_SLAB:
    case BIRTCH_SLAB:
    case JUNGLE_SLAB:
    case ACACIA_SLAB:
    case DARK_OAK_SLAB:
    case OAK_BUTTON:
    case PINE_BUTTON:
    case BIRTCH_BUTTON:
    case JUNGLE_BUTTON:
    case ACACIA_BUTTON:
    case DARK_OAK_BUTTON:
    case PINE_PRESSURE_PLATE:
    case BIRTCH_PRESSURE_PLATE:
    case JUNGLE_PRESSURE_PLATE:
    case ACACIA_PRESSURE_PLATE:
    case DARK_OAK_PRESSURE_PLATE:
    case CRAFTING_BENCH:
    case CRAFTING_BENCH_SIDE1:
    case CRAFTING_BENCH_SIDE2:
    case BOOK_CASE:
    case BED:
    case BEAT:
    case TORCH:
    case BURNT_OUT_TORCH:
    case BLACK_TORCH:
    case BLUE_TORCH:
    case EMPTY_POT:
    case CACTUS_IN_POT:
    case SINGLE_CHEST_TOP:
    case CHEST_SIDE:
    case SINGLE_CHEST_LATCH:
    case DOUBLE_CHEST_LEFT_LATCH:
    case DOUBLE_CHEST_RIGHT_LATCH:
    case DOUBLE_CHEST_RIGHT_BACK:
    case DOUBLE_CHEST_LEFT_BACK:
    case DOUBLE_CHEST_RIGHT_SIDE:
    case DOUBLE_CHEST_RIGHT_TOP:
    case DOUBLE_CHEST_LEFT_TOP:
        return "wood";
    // Sea life -> coral
    case BLUE_CORAL:
    case BLUE_CORAL2:
    case PURPLE_CORAL:
    case PURPLE_CORAL2:
    case PURPLE_CORAL3:
    case RED_CORAL:
    case RED_CORAL2:
    case YELLOW_CORAL:
    case YELLOW_CORAL2:
    case UNDERWATER_TUFT_SHORT:
    case UNDERWATER_TUFT_TALL:
    case SEAWEED:
        return "coral";
    // Glass + glowstone + lamps shatter
    case GLASS:
    case GLASS_PANE:
    case GLASS_BLACK:
    case GLASS_BLUE:
    case GLASS_BROWN:
    case GLASS_LIGHT_BLUE:
    case GLASS_GRAY:
    case GLASS_DARK_GREEN:
    case GLASS_LIGHT_GREEN:
    case GLASS_TINT_GREEN:
    case GLASS_PALE_BLUE:
    case GLASS_WHITE:
    case GLOWSTONE:
    case REDSTONE_LAMP:
    case REDSTONE_LAMP_POWERED:
        return "glass";
    case LADDER:
        return "ladder";
    default:
        return "stone";
    }
}

// Builds "<prefix>_<material>" in a small rotating buffer — safe because the
// caller (audio_play_sfx) only strcmps the name during the call.
static const char *block_pool( const char *prefix, const char *material ) {
    static char bufs[ 2 ][ 32 ];
    static int which = 0;
    char *buf = bufs[ which++ & 1 ];
    snprintf( buf, sizeof( bufs[ 0 ] ), "%s_%s", prefix, material );
    return buf;
}

const char *audio_block_dig_pool( int block_id ) {
    const char *mat = block_material( block_id );
    // The glass break pool is named "glass" (not "dig_glass") in the manifest,
    // and the pack has no dig/ladder — ladder digs sound like wood.
    if ( strcmp( mat, "glass" ) == 0 ) {
        return "glass";
    }
    if ( strcmp( mat, "ladder" ) == 0 ) {
        mat = "wood";
    }
    return block_pool( "dig", mat );
}

const char *audio_block_step_pool( int block_id ) {
    const char *mat = block_material( block_id );
    // No glass footstep files in the pack — glass steps sound like stone.
    return strcmp( mat, "glass" ) == 0 ? "step_stone" : block_pool( "step", mat );
}

#if defined( REPGAME_AUDIO )

#include <atomic>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "common/RepGame.hpp" // capture_mode
#include "common/RepGameSDL2.hpp" // SDL3 headers; SDL2 headers on WASM (its own backend below)
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

// Music is streamed: decoded float frames are pulled out of stb_vorbis on
// demand (the SDL3 audio thread via the music stream's get-callback; on WASM
// the device mix callback, which Emscripten runs on the main thread — see
// audio_mix_cb). One-shot SFX are fully decoded once; each play either binds
// a fresh stream to the device (SDL3 mixes all bound streams itself) or
// claims a voice slot the mix callback sums (WASM — SDL2 has no bindable
// device streams).
namespace {

SDL_AudioDeviceID audio_device = 0;
SDL_AudioSpec device_spec;
stb_vorbis *music_vorbis = nullptr;

bool audio_enabled = false;
bool music_enabled = true;
float music_gain = 1.0f;
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

float env_float( const char *name, float fallback ) {
    const char *env = getenv( name );
    if ( !env ) {
        return fallback;
    }
    const float v = atof( env );
    return v < 0.0f ? 0.0f : ( v > 1.0f ? 1.0f : v );
}

#if defined( REPGAME_WASM )

// --- SDL2 (Emscripten) backend ------------------------------------------------
// Emscripten's audio driver runs the device callback on the browser main
// thread from a ScriptProcessorNode (ProvidesOwnCallbackThread — there is no
// audio thread, and SDL_LockAudio is a no-op), so the callback, audio_tick
// and audio_play_sfx all run sequentially on one thread. SDL2 also has no
// device-bound streams, so audio_mix_cb mixes music + SFX itself. Sound data
// is not embedded (MK_BLOB is empty on WASM): MUSIC_TRACKS/SFX_SOUNDS carry
// "sounds/<name>.ogg" paths into the WASMFS preload instead of blobs.

unsigned char *music_file_data = nullptr; // kept alive while music_vorbis reads it
SDL_AudioStream *music_cvt = nullptr;     // source -> device format converter

// Fixed voice slots: data points into sfx_cache (not owned); data == nullptr
// marks the slot free.
struct SfxVoice {
    const float *data = nullptr;
    int frames = 0;
    int pos = 0;
};
SfxVoice voices[ 32 ];

static unsigned char *file_read_all( const char *path, int *out_len ) {
    FILE *f = fopen( path, "rb" );
    if ( !f ) {
        return nullptr;
    }
    fseek( f, 0, SEEK_END );
    const long len = ftell( f );
    fseek( f, 0, SEEK_SET );
    unsigned char *buf = ( unsigned char * )malloc( len > 0 ? len : 1 );
    if ( fread( buf, 1, len, f ) != ( size_t )len ) {
        free( buf );
        fclose( f );
        return nullptr;
    }
    fclose( f );
    *out_len = ( int )len;
    return buf;
}

static void music_close_track( ) {
    if ( music_vorbis ) {
        stb_vorbis_close( music_vorbis );
        music_vorbis = nullptr;
    }
    free( music_file_data );
    music_file_data = nullptr;
    if ( music_cvt ) {
        SDL_FreeAudioStream( music_cvt );
        music_cvt = nullptr;
    }
    current_track.store( -1 );
    music_want.store( true );
}

// Open the next queued track file as the current vorbis stream.
static void music_open_track( int idx ) {
    const MusicTrackEntry *track = &MUSIC_TRACKS[ idx ];
    int len = 0;
    int err = 0;
    music_file_data = file_read_all( track->path, &len );
    music_vorbis = music_file_data ? stb_vorbis_open_memory( music_file_data, len, &err, NULL ) : nullptr;
    if ( !music_vorbis ) {
        pr_debug( "audio: vorbis open failed for '%s' (%s) err=%d", track->title, track->path, err );
        free( music_file_data );
        music_file_data = nullptr;
        music_want.store( true );
        return;
    }
    const stb_vorbis_info info = stb_vorbis_get_info( music_vorbis );
    music_cvt = SDL_NewAudioStream( AUDIO_F32, info.channels, info.sample_rate,
                                    device_spec.format, device_spec.channels, device_spec.freq );
    if ( !music_cvt ) {
        pr_debug( "audio: converter create failed for '%s' (%s)", track->title, SDL_GetError( ) );
        stb_vorbis_close( music_vorbis );
        music_vorbis = nullptr;
        free( music_file_data );
        music_file_data = nullptr;
        music_want.store( true );
        return;
    }
    last_track = idx;
    current_track.store( idx );
    pr_debug( "audio: now playing %s", track->title );
}

// Device callback (main thread on Emscripten): sum the music stream (vorbis
// decoded through the rate converter) and every active SFX voice into the
// output buffer.
static void SDLCALL audio_mix_cb( void *userdata, Uint8 *stream, int len ) {
    memset( stream, 0, len );
    float *out = ( float * )stream;
    const int floats_total = len / ( int )sizeof( float );

    static float src_buf[ 4096 ];
    static float mix_buf[ 4096 ];
    int filled = 0;
    while ( filled < floats_total ) {
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
        if ( SDL_AudioStreamAvailable( music_cvt ) <= 0 ) {
            // Converter ran dry — push more vorbis frames into it.
            const stb_vorbis_info info = stb_vorbis_get_info( music_vorbis );
            int want_src = ( int )( sizeof( src_buf ) / sizeof( float ) );
            want_src -= want_src % info.channels;
            const int got = stb_vorbis_get_samples_float_interleaved( music_vorbis, info.channels, src_buf, want_src );
            if ( got > 0 ) {
                if ( SDL_AudioStreamPut( music_cvt, src_buf, got * info.channels * ( int )sizeof( float ) ) < 0 ) {
                    music_close_track( );
                    continue;
                }
            } else {
                SDL_AudioStreamFlush( music_cvt );
                if ( SDL_AudioStreamAvailable( music_cvt ) <= 0 ) {
                    // Track fully decoded and drained — move on.
                    music_close_track( );
                }
            }
            continue;
        }
        int want = floats_total - filled;
        const int avail = SDL_AudioStreamAvailable( music_cvt ) / ( int )sizeof( float );
        if ( want > avail ) {
            want = avail;
        }
        const int buf_cap = ( int )( sizeof( mix_buf ) / sizeof( float ) );
        if ( want > buf_cap ) {
            want = buf_cap;
        }
        const int got_bytes = SDL_AudioStreamGet( music_cvt, mix_buf, want * ( int )sizeof( float ) );
        if ( got_bytes <= 0 ) {
            break;
        }
        const int n = got_bytes / ( int )sizeof( float );
        for ( int i = 0; i < n; i++ ) {
            out[ filled + i ] += mix_buf[ i ] * music_gain;
        }
        filled += n;
    }

    const int frames_total = floats_total / device_spec.channels;
    for ( SfxVoice &v : voices ) {
        if ( !v.data ) {
            continue;
        }
        int n_frames = v.frames - v.pos;
        if ( n_frames > frames_total ) {
            n_frames = frames_total;
        }
        const float *src = v.data + v.pos * device_spec.channels;
        const int n = n_frames * device_spec.channels;
        for ( int i = 0; i < n; i++ ) {
            out[ i ] += src[ i ] * sfx_gain;
        }
        v.pos += n_frames;
        if ( v.pos >= v.frames ) {
            v.data = nullptr;
        }
    }
}

// Decode a whole SFX file and convert it to the device format up front, so
// mixing in the callback is a plain sum.
static bool sfx_decode( int idx, SfxPcm *out ) {
    int len = 0;
    int err = 0;
    unsigned char *file_data = file_read_all( SFX_SOUNDS[ idx ].path, &len );
    stb_vorbis *v = file_data ? stb_vorbis_open_memory( file_data, len, &err, NULL ) : nullptr;
    if ( !v ) {
        pr_debug( "audio: vorbis open failed for sfx '%s' (%s) err=%d", SFX_SOUNDS[ idx ].base, SFX_SOUNDS[ idx ].path, err );
        free( file_data );
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info( v );
    const int total_frames = ( int )stb_vorbis_stream_length_in_samples( v );
    float *raw = ( float * )malloc( total_frames * info.channels * sizeof( float ) );
    const int got = stb_vorbis_get_samples_float_interleaved( v, info.channels, raw, total_frames * info.channels );
    stb_vorbis_close( v );
    free( file_data );
    if ( got <= 0 ) {
        free( raw );
        return false;
    }
    SDL_AudioStream *cvt = SDL_NewAudioStream( AUDIO_F32, info.channels, info.sample_rate,
                                             device_spec.format, device_spec.channels, device_spec.freq );
    if ( !cvt ) {
        pr_debug( "audio: converter create failed for sfx '%s' (%s)", SFX_SOUNDS[ idx ].base, SDL_GetError( ) );
        free( raw );
        return false;
    }
    SDL_AudioStreamPut( cvt, raw, got * info.channels * ( int )sizeof( float ) );
    SDL_AudioStreamFlush( cvt );
    free( raw );
    const int avail = SDL_AudioStreamAvailable( cvt );
    out->data = ( float * )malloc( avail > 0 ? avail : 1 );
    const int got_bytes = SDL_AudioStreamGet( cvt, out->data, avail );
    SDL_FreeAudioStream( cvt );
    if ( got_bytes <= 0 ) {
        free( out->data );
        out->data = nullptr;
        return false;
    }
    out->frames = got_bytes / ( int )sizeof( float ) / device_spec.channels;
    out->channels = device_spec.channels;
    out->rate = device_spec.freq;
    out->decoded = true;
    return out->frames > 0;
}

#else

SDL_AudioStream *music_stream = nullptr;
std::vector<SDL_AudioStream *> active_sfx;

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

#endif

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
    music_gain = env_float( "REPGAME_MUSIC_VOLUME", 0.5f );
    sfx_gain = env_float( "REPGAME_SFX_VOLUME", 1.0f );
    // Seconds of silence between tracks, uniform in [gap/4, gap].
    // REPGAME_MUSIC_GAP=0 = continuous shuffle.
    const char *env_gap = getenv( "REPGAME_MUSIC_GAP" );
    const float gap_s = env_gap ? atof( env_gap ) : 240.0f;
    gap_min_ticks = ( long )( gap_s * 0.25f * UPS_RATE );
    gap_span_ticks = ( long )( gap_s * 0.75f * UPS_RATE ) + 1;

#if defined( REPGAME_WASM )
    // Emscripten's SDL2 backend forces the device rate to the AudioContext
    // sample rate and only supports AUDIO_F32; want.samples sizes the
    // ScriptProcessorNode buffer. The context starts suspended — SDL2
    // resumes it automatically once the user has interacted with the page.
    SDL_AudioSpec want;
    SDL_zero( want );
    want.freq = 48000;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_mix_cb;
    audio_device = SDL_OpenAudioDevice( NULL, 0, &want, &device_spec, 0 );
    if ( !audio_device ) {
        pr_debug( "audio: no playback device (%s), running silent", SDL_GetError( ) );
        return;
    }

    const int num_sfx = ( int )( sizeof( SFX_SOUNDS ) / sizeof( SFX_SOUNDS[ 0 ] ) ) - 1; // NULL terminator row
    sfx_cache.resize( num_sfx > 0 ? num_sfx : 0 );

    audio_enabled = true;
    music_want.store( true );
    SDL_PauseAudioDevice( audio_device, 0 );
    pr_debug( "audio: device %dHz %dch, %d music tracks, %d sfx", device_spec.freq, device_spec.channels,
              ( int )( sizeof( MUSIC_TRACKS ) / sizeof( MUSIC_TRACKS[ 0 ] ) ) - 1, num_sfx );
#else
    // Open the device directly, NOT via SDL_OpenAudioDeviceStream — a device
    // opened by that call refuses any further stream bindings, so the
    // one-shot SFX streams could never be bound and were always silent.
    // A small device buffer keeps SFX latency low (driver default can be
    // ~100ms on PipeWire/PulseAudio, which is audible on water splashes).
    SDL_SetHint( SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512" );
    const SDL_AudioSpec want = { SDL_AUDIO_F32, 2, 48000 };
    audio_device = SDL_OpenAudioDevice( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want );
    if ( !audio_device ) {
        pr_debug( "audio: no playback device (%s), running silent", SDL_GetError( ) );
        return;
    }
    SDL_GetAudioDeviceFormat( audio_device, &device_spec, NULL );
    music_stream = SDL_CreateAudioStream( &want, &device_spec );
    if ( !music_stream ) {
        pr_debug( "audio: stream create failed (%s), running silent", SDL_GetError( ) );
        SDL_CloseAudioDevice( audio_device );
        audio_device = 0;
        return;
    }
    SDL_SetAudioStreamGetCallback( music_stream, music_feed, NULL );
    if ( !SDL_BindAudioStream( audio_device, music_stream ) ) {
        pr_debug( "audio: music bind failed (%s), running silent", SDL_GetError( ) );
        SDL_DestroyAudioStream( music_stream );
        music_stream = nullptr;
        SDL_CloseAudioDevice( audio_device );
        audio_device = 0;
        return;
    }
    SDL_SetAudioStreamGain( music_stream, music_gain );

    const int num_sfx = ( int )( sizeof( SFX_SOUNDS ) / sizeof( SFX_SOUNDS[ 0 ] ) ) - 1; // NULL terminator row
    sfx_cache.resize( num_sfx > 0 ? num_sfx : 0 );

    audio_enabled = true;
    music_want.store( true );
    SDL_ResumeAudioDevice( audio_device );
    pr_debug( "audio: device %dHz %dch, %d music tracks, %d sfx", device_spec.freq, device_spec.channels,
              ( int )( sizeof( MUSIC_TRACKS ) / sizeof( MUSIC_TRACKS[ 0 ] ) ) - 1, num_sfx );
#endif
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
#if !defined( REPGAME_WASM )
    // Reap finished one-shots: once a flushed stream has no queued input and
    // no converted output left, the device already consumed every sample.
    // (WASM voices free themselves in the mix callback — nothing to reap.)
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
#endif
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
        pr_debug( "audio: no sfx variants for '%s'", base_name );
        return;
    }
    const int idx = variants[ rand( ) % num_variants ];
    SfxPcm &pcm = sfx_cache[ idx ];
    if ( !pcm.decoded && !sfx_decode( idx, &pcm ) ) {
        pr_debug( "audio: decode failed for sfx '%s'", SFX_SOUNDS[ idx ].base );
        return;
    }
#if defined( REPGAME_WASM )
    // Claim a free voice slot; the mix callback plays the (already
    // device-format) PCM. Voices point into the cache — no copy.
    for ( SfxVoice &v : voices ) {
        if ( !v.data ) {
            v.data = pcm.data;
            v.frames = pcm.frames;
            v.pos = 0;
            return;
        }
    }
    pr_debug( "audio: no free voice for sfx '%s'", SFX_SOUNDS[ idx ].base );
#else
    const SDL_AudioSpec src = { SDL_AUDIO_F32, ( Uint8 )pcm.channels, pcm.rate };
    SDL_AudioStream *s = SDL_CreateAudioStream( &src, &device_spec );
    if ( !s ) {
        pr_debug( "audio: stream create failed: %s", SDL_GetError( ) );
        return;
    }
    SDL_SetAudioStreamGain( s, sfx_gain );
    if ( !SDL_PutAudioStreamData( s, pcm.data, pcm.frames * pcm.channels * ( int )sizeof( float ) ) ) {
        pr_debug( "audio: put failed for sfx '%s': %s", SFX_SOUNDS[ idx ].base, SDL_GetError( ) );
        SDL_DestroyAudioStream( s );
        return;
    }
    SDL_FlushAudioStream( s );
    if ( !SDL_BindAudioStream( audio_device, s ) ) {
        pr_debug( "audio: bind failed for sfx '%s': %s", SFX_SOUNDS[ idx ].base, SDL_GetError( ) );
        SDL_DestroyAudioStream( s );
        return;
    }
    active_sfx.push_back( s );
#endif
}

void audio_set_music_gain( float gain ) {
    music_gain = gain;
#if !defined( REPGAME_WASM )
    if ( music_stream ) {
        SDL_SetAudioStreamGain( music_stream, gain );
    }
#endif
}

void audio_set_sfx_gain( float gain ) {
    sfx_gain = gain;
}

const char *audio_current_track_title( ) {
    const int idx = current_track.load( );
    return idx < 0 ? nullptr : MUSIC_TRACKS[ idx ].title;
}

void audio_cleanup( ) {
#if defined( REPGAME_WASM )
    if ( audio_device ) {
        SDL_CloseAudioDevice( audio_device ); // stops the mix callback first
        audio_device = 0;
    }
    music_close_track( );
    for ( SfxVoice &v : voices ) {
        v.data = nullptr;
    }
#else
    for ( SDL_AudioStream *s : active_sfx ) {
        SDL_UnbindAudioStream( s );
        SDL_DestroyAudioStream( s );
    }
    active_sfx.clear( );
    if ( music_stream ) {
        SDL_UnbindAudioStream( music_stream );
        SDL_DestroyAudioStream( music_stream );
        music_stream = nullptr;
    }
    if ( audio_device ) {
        SDL_CloseAudioDevice( audio_device );
        audio_device = 0;
    }
#endif
    for ( SfxPcm &pcm : sfx_cache ) {
        free( pcm.data );
    }
    sfx_cache.clear( );
    current_track.store( -1 );
    audio_enabled = false;
}

#endif
