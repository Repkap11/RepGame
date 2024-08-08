#include <jni.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "common/RepGame.hpp"

AAssetManager *assetManager;
RepGame repgame;

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
    env->ReleaseStringUTFChars( world_name_java, world_name );
    next_game_step = now_ms( );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceDestroyed( JNIEnv *env, jobject obj ) {
    repgame.cleanup( );
}

int current_screen_width = 0;
int current_screen_height = 0;

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSizeChanged( JNIEnv *env, jobject obj, jint width, jint height ) {
    current_screen_width = width;
    current_screen_height = height;
    repgame.changeSize( ( int )width, ( int )height );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame( JNIEnv *env, jobject obj ) {
    int now = now_ms( );
    int computer_is_too_slow_limit = 10; // max number of advances per render, if you can't get 20 fps, slow the game's UPS
    int num_ticks_in_frame = 0;
    while ( ( ( ( ( int )next_game_step - ( int )now ) <= 0 ) ) && ( computer_is_too_slow_limit-- ) ) {
        repgame.tick( );
        num_ticks_in_frame++;
        next_game_step += ( 1000 / UPS_RATE ); // count 1 game tick done
    }
    // pr_debug( "slow:%d num_ticks_in_frame:%d fps:%f", computer_is_too_slow_limit, num_ticks_in_frame, ( float )( UPS_RATE ) / ( float )num_ticks_in_frame );

    repgame.clear( );
    repgame.draw( );
}

#define ANDROID_PAN_SENSITIVITY 2

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_lookInput( JNIEnv *env, jobject obj, jint xdiff, jint ydiff ) {
    Input &input = repgame.getInputState( );
    input.lookMove(current_screen_width / 2 + xdiff * ANDROID_PAN_SENSITIVITY, current_screen_height / 2 + ydiff * ANDROID_PAN_SENSITIVITY );
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
