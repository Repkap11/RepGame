#include <jni.h>
#include <android/log.h>
#include "RepGame.h"

void pr_debug( const char *fmt, ... ) {
    va_list myargs;
    va_start( myargs, fmt );
    __android_log_vprint( ANDROID_LOG_INFO, "RepGameAndroid", fmt, myargs );
    va_end( myargs );
}

void on_surface_created( );

void on_surface_changed( int i, int i1 );

void on_draw_frame( );

extern "C" {

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceCreated( JNIEnv *env, jobject obj ) {
    on_surface_created( );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceChanged( JNIEnv *env, jobject obj, jint width, jint height ) {
    on_surface_changed( 0, 0 );
}

JNIEXPORT void JNICALL Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame( JNIEnv *env, jobject obj ) {
    on_draw_frame( );
}
}

void on_surface_created( ) {
    pr_debug( "################################# START #################################" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    glClearColor( 1.0f, 0.0f, 0.0f, 0.0f );
}

void on_draw_frame( ) {
    glClear( GL_COLOR_BUFFER_BIT );
}

void on_surface_changed( int width, int height ) {
}
