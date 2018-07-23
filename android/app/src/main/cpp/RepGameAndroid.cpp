#include "RepGame.h"

#include <jni.h>
#include <stdarg.h>
#include <android/log.h>

void pr_debug( const char *fmt, ... ) {
    va_list myargs;
    va_start( myargs, fmt );
    __android_log_vprint( ANDROID_LOG_INFO, "RepGameAndroid", fmt, myargs );
    va_end( myargs );
}

void on_surface_created( unsigned char *textures, int textures_len );

void on_surface_changed( int i, int i1 );

void on_draw_frame( );

extern "C" {

unsigned char *as_unsigned_char_array( JNIEnv *env, jbyteArray array, int *out_size ) {
    int len = env->GetArrayLength( array );
    unsigned char *buf = new unsigned char[ len ];
    env->GetByteArrayRegion( array, 0, len, reinterpret_cast<jbyte *>( buf ) );
    *out_size = len;
    return buf;
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceCreated( JNIEnv *env, jobject obj, jbyteArray array ) {
    int textures_len;
    unsigned char *textures = as_unsigned_char_array( env, array, &textures_len );
    pr_debug( "################################# START #################################" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    repgame_set_textures( textures, textures_len );
    repgame_init( );
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

#define ANDROID_PAN_SENSITIVITY 2

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_lookInput( JNIEnv *env, jobject obj, jint xdiff, jint ydiff ) {
    input_lookMove( repgame_getInputState( ), current_screen_width / 2 + xdiff * ANDROID_PAN_SENSITIVITY, current_screen_height / 2 + ydiff * ANDROID_PAN_SENSITIVITY );
}

#define ANDROID_MOVE_SENSITIVITY 1
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_positionHInput( JNIEnv *env, jobject obj, jfloat sizeH, jfloat angleH ) {
    input_positionHMove( repgame_getInputState( ), sizeH * ANDROID_MOVE_SENSITIVITY, angleH );
}
JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_positionVInput( JNIEnv *env, jobject obj, jint sizeV ) {
    input_positionVMove( repgame_getInputState( ), sizeV * ANDROID_MOVE_SENSITIVITY );
}

} // End Extern C
