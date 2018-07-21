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
    int size;
    unsigned char *buf = as_unsigned_char_array( env, array, &size );
    on_surface_created( buf, size );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceChanged( JNIEnv *env, jobject obj, jint width, jint height ) {
    on_surface_changed( 0, 0 );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame( JNIEnv *env, jobject obj ) {
    on_draw_frame( );
}
}

void on_surface_created( unsigned char *textures, int textures_len ) {
    pr_debug( "################################# START #################################" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    repgame_set_textures( textures, textures_len );
    repgame_init( );
    repgame_changeSize( 1920, 1080 );

    // while ( !repgame_shouldExit( ) ) {
    //     int width, height;
    //     repgame_get_screen_size( &width, &height );

    //     // showErrors( );
    // }
    // repgame_cleanup( );
}

void on_draw_frame( ) {
    repgame_clear( );
    repgame_tick( );
    repgame_draw( );
}

void on_surface_changed( int width, int height ) {
    repgame_changeSize( width, height );
}
