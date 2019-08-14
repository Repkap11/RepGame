#include "common/RepGame.hpp"

#include <jni.h>
#include <stdarg.h>
#include <string.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

AAssetManager *assetManager;

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

unsigned char *as_unsigned_char_array( JNIEnv *env, jbyteArray array, int *out_size ) {
    int len = env->GetArrayLength( array );
    unsigned char *buf = new unsigned char[ len ];
    env->GetByteArrayRegion( array, 0, len, reinterpret_cast<jbyte *>( buf ) );
    *out_size = len;
    return buf;
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceCreated( JNIEnv *env, jobject obj, jbyteArray texture_bytes0, jbyteArray texture_bytes1, jobject assetManager_java, jstring world_name_java ) {
    pr_debug( "################################# START #################################" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    int textures_len;
    unsigned char *texture;
    const char *world_name = env->GetStringUTFChars( world_name_java, 0 );

    texture = as_unsigned_char_array( env, texture_bytes0, &textures_len );
    repgame_set_textures( 0, texture, textures_len );
    texture = as_unsigned_char_array( env, texture_bytes1, &textures_len );
    repgame_set_textures( 1, texture, textures_len );
    assetManager = AAssetManager_fromJava( env, assetManager_java );

    repgame_init( world_name );
    env->ReleaseStringUTFChars( world_name_java, world_name );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceDestroyed( JNIEnv *env, jobject obj ) {
    repgame_cleanup( );
}

int current_screen_width = 0;
int current_screen_height = 0;

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSizeChanged( JNIEnv *env, jobject obj, jint width, jint height ) {
    current_screen_width = width;
    current_screen_height = height;
    repgame_changeSize( ( int )width, ( int )height );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame( JNIEnv *env, jobject obj ) {
    repgame_clear( );
    repgame_tick( );
    repgame_draw( );
}

#define ANDROID_PAN_SENSITIVITY 5

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_lookInput( JNIEnv *env, jobject obj, jint xdiff, jint ydiff ) {
    input_lookMove( repgame_getInputState( ), current_screen_width / 2 + xdiff * ANDROID_PAN_SENSITIVITY, current_screen_height / 2 + ydiff * ANDROID_PAN_SENSITIVITY );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_setButtonState( JNIEnv *env, jobject obj, jint left, jint middle, jint right ) {
    input_setButtonState( repgame_getInputState( ), left, middle, right );
}

#define ANDROID_MOVE_SENSITIVITY 1
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_positionHInput( JNIEnv *env, jobject obj, jfloat sizeH, jfloat angleH ) {
    input_positionHMove( repgame_getInputState( ), sizeH * ANDROID_MOVE_SENSITIVITY, angleH );
}
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_positionVInput( JNIEnv *env, jobject obj, jint sizeV ) {
    input_positionVMove( repgame_getInputState( ), sizeV * ANDROID_MOVE_SENSITIVITY );
}

} // End Extern C
