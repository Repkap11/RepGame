package com.repkap11.repgame;
import static android.opengl.GLES20.*;
import android.opengl.GLSurfaceView;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Created by paul on 10/22/17.
 */

class RendererWrapper implements GLSurfaceView.Renderer {
    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        RepGameJNIWrapper.onSurfaceCreated();
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        RepGameJNIWrapper.onSurfaceChanged();
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        RepGameJNIWrapper.onDrawFrame();
    }
}

