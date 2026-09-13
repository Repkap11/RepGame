#include <jni.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "common/RepGame.hpp"

static AAssetManager *assetManager;
static RepGame repgame;

char *repgame_android_getShaderString( const char *filename ) {
    AAssetDir *assetDir = AAssetManager_openDir( assetManager, "" );
    AAsset *asset = AAssetManager_open( assetManager, filename, AASSET_MODE_STREAMING );

    int size = ( int )AAsset_getLength( asset );
    size += 1;
    char *buf = ( char * )malloc( size );

    pr_debug( "About to asset %s size:%d", filename, size );

    /* read the entire file into a string */
    int read_size = AAsset_read( asset, buf, size );
    pr_debug( "Read asset size:%d out of %d", read_size, size );
    buf[ read_size ] = '\0';

    AAsset_close( asset );
    AAssetDir_close( assetDir );
    pr_debug( "Done asset" );
    return buf;
}

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

unsigned char *as_unsigned_char_array( JNIEnv *env, jbyteArray array, int *out_size ) {
    int len = env->GetArrayLength( array );
    unsigned char *buf = new unsigned char[ len ];
    env->GetByteArrayRegion( array, 0, len, reinterpret_cast<jbyte *>( buf ) );
    *out_size = len;
    return buf;
}
int next_game_step;

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceCreated( JNIEnv *env, jobject obj, jbyteArray texture_bytes0, jbyteArray texture_bytes1, jobject assetManager_java, jstring world_name_java ) {
    pr_debug( "################################# START #################################" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    int textures_len;
    unsigned char *texture;
    const char *world_name = env->GetStringUTFChars( world_name_java, 0 );

    texture = as_unsigned_char_array( env, texture_bytes0, &textures_len );
    repgame.set_textures( 0, texture, textures_len );
    texture = as_unsigned_char_array( env, texture_bytes1, &textures_len );
    repgame.set_textures( 1, texture, textures_len );
    assetManager = AAssetManager_fromJava( env, assetManager_java );

    repgame.init( world_name, true, "repkap11.com", true );
    // repgame.init( world_name, true, "192.168.1.8", true );
    env->ReleaseStringUTFChars( world_name_java, world_name );
    next_game_step = now_ms( );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceDestroyed( JNIEnv *env, jobject obj ) {
    repgame.cleanup( );
}

static int current_screen_width = 0;
static int current_screen_height = 0;

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSizeChanged( JNIEnv *env, jobject obj, jint width, jint height ) {
    current_screen_width = width;
    current_screen_height = height;
    repgame.changeSize( ( int )width, ( int )height );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame( JNIEnv *env, jobject obj ) {
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

#define ANDROID_PAN_SENSITIVITY 1.25f

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_lookInput( JNIEnv *env, jobject obj, jint xdiff, jint ydiff ) {
    Input &input = repgame.getInputState( );
    input.lookMove(xdiff * ANDROID_PAN_SENSITIVITY, ydiff * ANDROID_PAN_SENSITIVITY );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_setButtonState( JNIEnv *env, jobject obj, jint left, jint middle, jint right ) {
    Input &input = repgame.getInputState( );
    input.setButtonState(left, middle, right );
}

#define ANDROID_MOVE_SENSITIVITY 1
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_positionHInput( JNIEnv *env, jobject obj, jfloat sizeH, jfloat angleH ) {
    Input &input = repgame.getInputState( );
    input.positionHMove(sizeH * ANDROID_MOVE_SENSITIVITY, angleH );
}
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_setJumpPressed( JNIEnv *env, jobject obj, jint jumpPressed ) {
    Input &input = repgame.getInputState( );
    input.setJumpPressed(jumpPressed );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onInventoryClicked( JNIEnv *env, jobject obj ) {
    Input &input = repgame.getInputState( );
    repgame.getInputState( ).onInventoryClicked( );
}

} // End Extern C
