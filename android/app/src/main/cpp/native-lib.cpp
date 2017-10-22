#include <jni.h>
#include  <GLES2/gl2.h>

void on_surface_created();

void on_surface_changed(int i, int i1);

void on_draw_frame();

extern "C" {


    JNIEXPORT void JNICALL
    Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceCreated(JNIEnv *env, jobject obj) {
        on_surface_created();
    }

    JNIEXPORT void JNICALL
    Java_com_repkap11_repgame_RepGameJNIWrapper_onSurfaceChanged(JNIEnv *env, jobject obj, jint width, jint height) {
        on_surface_changed(0, 0);
    }

    JNIEXPORT void JNICALL
    Java_com_repkap11_repgame_RepGameJNIWrapper_onDrawFrame(JNIEnv *env, jobject obj) {
        on_draw_frame();
    }
}

void on_surface_created() {
    glClearColor(1.0f, 0.0f, 0.0f, 0.0f);

}

void on_draw_frame() {
    glClear(GL_COLOR_BUFFER_BIT);
}

void on_surface_changed(int width, int height) {

}

