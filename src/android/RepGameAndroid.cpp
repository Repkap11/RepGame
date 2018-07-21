#include <jni.h>
#include <android/log.h>
#include "RepGame.h"

#define APPNAME "RepGameNative"

#define pr_debug( message, ... ) __android_log_print( ANDROID_LOG_INFO, APPNAME, message, ##__VA_ARGS__ );

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
