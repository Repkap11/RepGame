#include <jni.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <mutex>

#include "common/RepGame.hpp"

static RepGame repgame;

extern "C" {

static double now_ms( void ) {
    struct timespec res;
    clock_gettime( CLOCK_MONOTONIC, &res );
    return ( int )( 1000.0 * res.tv_sec + ( double )res.tv_nsec / 1e6 );
}

static inline long long now_us( void ) {
    struct timespec res;
    clock_gettime( CLOCK_MONOTONIC, &res );
    return ( long long )res.tv_sec * 1000000LL + ( long long )res.tv_nsec / 1000LL;
}

int next_game_step;

// The engine is init-once: globalGameState is process-global, so a
// second onSurfaceCreated in the same process (a second Activity
// instance created before the first dies, a recreated EGL context) can
// never re-init. g_engine_state is claimed *before* init() runs —
// observed logs show the second surface arriving on a different GL
// thread while the first init is still in-flight, so a flag set after
// init() returns is too late. Anything other than ENGINE_READY makes
// the JNI entry points below no-op.
enum EngineState { ENGINE_NEW = 0, ENGINE_STARTING = 1, ENGINE_READY = 2, ENGINE_DEAD = 3 };
static std::atomic<int> g_engine_state{ ENGINE_NEW };

// RepGame::cleanup() is not re-entrant: its clean_up_done flag is only
// set when teardown finishes, so concurrent calls (e.g. the GL-thread
// restart path racing a dying Activity's onSurfaceDestroyed) both enter
// and double-join the terrain worker threads. Serialize it — the first
// caller runs the save/teardown and later callers wait for it to
// finish, so the process never exits mid-save.
static std::mutex g_cleanup_mutex;
static bool g_cleanup_done = false;

static void cleanup_once( ) {
    std::lock_guard<std::mutex> lock( g_cleanup_mutex );
    if ( !g_cleanup_done ) {
        repgame.cleanup( );
        g_cleanup_done = true;
    }
}

JNIEXPORT jboolean JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceCreated( JNIEnv *env, jobject obj, jstring world_name_java ) {
    int prior = g_engine_state.exchange( ENGINE_STARTING );
    if ( prior != ENGINE_NEW ) {
        pr_debug( "GL surface recreated in a live process; engine can't re-init — saving and restarting" );
        // A completed init can save+teardown; a still-running init can't
        // be safely cleaned, so just mark dead — the relaunching process
        // exit takes the half-initialized engine down with it.
        if ( prior == ENGINE_READY ) {
            cleanup_once( );
        }
        g_engine_state = ENGINE_DEAD;
        return JNI_FALSE;
    }
    pr_debug( "################################# START #################################" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    const char *world_name = env->GetStringUTFChars( world_name_java, 0 );

    repgame.init( world_name, true, "repkap11.com", true );
    // repgame.init( world_name, true, "192.168.1.8", true );
    env->ReleaseStringUTFChars( world_name_java, world_name );
    // A second surface that arrived mid-init already latched ENGINE_DEAD
    // — don't resurrect it.
    int expected = ENGINE_STARTING;
    g_engine_state.compare_exchange_strong( expected, ENGINE_READY );
    next_game_step = now_ms( );
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceDestroyed( JNIEnv *env, jobject obj ) {
    // ENGINE_NEW means a surface was destroyed before one was ever
    // created — nothing to tear down.
    if ( g_engine_state.load( ) != ENGINE_NEW ) {
        cleanup_once( );
    }
    g_engine_state = ENGINE_DEAD;
}

static int current_screen_width = 0;
static int current_screen_height = 0;

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSizeChanged( JNIEnv *env, jobject obj, jint width, jint height ) {
    if ( g_engine_state.load( ) != ENGINE_READY ) {
        return;
    }
    current_screen_width = width;
    current_screen_height = height;
    repgame.changeSize( ( int )width, ( int )height );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame( JNIEnv *env, jobject obj ) {
    // Held for the whole frame so a restart-path cleanup_once() on another
    // GL thread can't tear down the engine mid-frame; the state check runs
    // under the lock because that path leaves READY before cleaning up.
    std::lock_guard<std::mutex> lock( g_cleanup_mutex );
    if ( g_engine_state.load( ) != ENGINE_READY ) {
        return;
    }
    int now = now_ms( );
    int computer_is_too_slow_limit = 10; // max number of advances per render, if you can't get 20 fps, slow the game's UPS
    int num_ticks_in_frame = 0;
    constexpr int time_step_ms = 1000 / UPS_RATE;
    const long long t_tick_start = now_us( );
    while ( ( ( ( ( int )next_game_step - ( int )now ) <= 0 ) ) && ( computer_is_too_slow_limit-- ) ) {
        repgame.tick( );
        num_ticks_in_frame++;
        next_game_step += time_step_ms; // count 1 game tick done
    }
    const long long us_tick = now_us( ) - t_tick_start;
    // pr_debug( "slow:%d num_ticks_in_frame:%d fps:%f", computer_is_too_slow_limit, num_ticks_in_frame, ( float )( UPS_RATE ) / ( float )num_ticks_in_frame );

    // Interpolation factor between the last executed tick and the next scheduled tick.
    const int render_now = now_ms( );
    float alpha = static_cast<float>( render_now - ( ( int )next_game_step - time_step_ms ) ) / static_cast<float>( time_step_ms );
    if ( alpha < 0.0f ) {
        alpha = 0.0f;
    } else if ( alpha > 1.0f ) {
        alpha = 1.0f;
    }

    repgame.clear( );
    repgame.draw( alpha );

    static int fps_frame_count = 0;
    static long fps_last_report_time = now_ms( );
    static long long acc_tick = 0;
    static long long acc_render = 0;
    static long long acc_world_draw = 0;
    static long long acc_ui_draw = 0;
    static long long acc_total_draw = 0;
    static int acc_remeshed = 0;
    static int max_drawable = 0;
    fps_frame_count++;
    acc_tick += us_tick;
    acc_render += repgame.profiling.us_render;
    acc_world_draw += repgame.profiling.us_world_draw;
    acc_ui_draw += repgame.profiling.us_ui_draw;
    acc_total_draw += repgame.profiling.us_total_draw;
    acc_remeshed += repgame.profiling.num_chunks_remeshed;
    if ( repgame.profiling.num_drawable_chunks > max_drawable ) {
        max_drawable = repgame.profiling.num_drawable_chunks;
    }
    const long fps_now = now_ms( );
    const long fps_elapsed = fps_now - fps_last_report_time;
    if ( fps_elapsed >= 1000 ) {
        const float fps = static_cast<float>( fps_frame_count * 1000 ) / static_cast<float>( fps_elapsed );
        const float inv_frames = 1.0f / static_cast<float>( fps_frame_count );
        pr_debug( "FPS: %.1f | tick:%.2fms render:%.2fms worldDraw:%.2fms ui:%.2fms totalDraw:%.2fms | chunks:%d remeshed:%d ticks/frame:%.1f",
            fps,
            acc_tick * inv_frames / 1000.0f,
            acc_render * inv_frames / 1000.0f,
            acc_world_draw * inv_frames / 1000.0f,
            acc_ui_draw * inv_frames / 1000.0f,
            acc_total_draw * inv_frames / 1000.0f,
            max_drawable,
            acc_remeshed,
            ( float )num_ticks_in_frame );
        fps_frame_count = 0;
        fps_last_report_time = fps_now;
        acc_tick = acc_render = acc_world_draw = acc_ui_draw = acc_total_draw = 0;
        acc_remeshed = 0;
        max_drawable = 0;
    }
}

// All input arrives as raw touches: Java forwards every MotionEvent here
// (on the GL thread via GLSurfaceView.queueEvent) and the shared C++
// TouchControls module handles control routing, hit testing and taps.
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_touchEvent( JNIEnv *env, jobject obj, jint phase, jlong id, jfloat x, jfloat y ) {
    if ( g_engine_state.load( ) != ENGINE_READY ) {
        return;
    }
    repgame.touch_event( static_cast<TouchPhase>( phase ), static_cast<int64_t>( id ), x, y );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_backPressed( JNIEnv *env, jobject obj ) {
    if ( g_engine_state.load( ) != ENGINE_READY ) {
        return;
    }
    repgame.getInputState( ).backPressed( );
}

JNIEXPORT jboolean JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_shouldExit( JNIEnv *env, jobject obj ) {
    return repgame.shouldExit( ) ? JNI_TRUE : JNI_FALSE;
}

} // End Extern C
