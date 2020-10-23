package com.repkap11.repgame;

import android.content.Context;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.util.Log;

import com.google.vr.sdk.base.Eye;
import com.google.vr.sdk.base.GvrView;
import com.google.vr.sdk.base.HeadTransform;
import com.google.vr.sdk.base.Viewport;

import java.io.InputStream;
import java.io.File;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.opengles.GL10;

/**
 * Created by paul on 10/22/17.
 */

public class RepGameAndroidRenderer implements GvrView.Renderer {
    static private final String TAG = RepGameAndroidRenderer.class.getSimpleName();
    private final Context mApplicationContext;

    public RepGameAndroidRenderer(Context applicationContext) {
        mApplicationContext = applicationContext;
    }

    private float[] heads;
    @Override
    public void onDrawFrame(HeadTransform headTransform, Eye eye, Eye eye1) {
        RepGameJNIWrapper.setHeadForVR(headTransform.getHeadView());
        RepGameJNIWrapper.onDrawFrame();
    }

    @Override
    public void onFinishFrame(Viewport viewport) {

    }

    @Override
    public void onRendererShutdown() {

    }

    @Override
    public void onSurfaceCreated(EGLConfig config) {
        InputStream blocksIS = mApplicationContext.getResources().openRawResource(R.raw.textures);
        InputStream skyIS = mApplicationContext.getResources().openRawResource(R.raw.sky4);
        byte[] blocks = null;
        byte[] sky = null;
        try {
            blocks = new byte[blocksIS.available()];
            sky = new byte[skyIS.available()];
            blocksIS.read(blocks);
            skyIS.read(sky);
        } catch (Exception e) {
            Log.d(TAG, "Failed to read bitmap");
        }

        RepGameJNIWrapper.onSurfaceCreated(blocks, sky, mApplicationContext.getAssets(), mApplicationContext.getFilesDir().getPath() + File.separator + "World1");
    }

    @Override
    public void onSurfaceChanged(int width, int height) {
        RepGameJNIWrapper.onSizeChanged(width, height);
    }

    public void lookInput(int xdiff, int ydiff) {
        RepGameJNIWrapper.lookInput(xdiff, ydiff);
    }

    public void positionHInput(float sizeH, float angleH) {
        RepGameJNIWrapper.positionHInput(sizeH, angleH);
    }
    public void setButtonState(int left, int middle, int right){
        RepGameJNIWrapper.setButtonState(left, middle, right);
    }


    public void setJumpPressed(int jumpPressed) {
        RepGameJNIWrapper.setJumpPressed(jumpPressed);
    }

    public void onPause() {
        RepGameJNIWrapper.onSurfaceDestroyed();
    }

    public static class ConfigChooser implements GLSurfaceView.EGLConfigChooser {
        @Override
        public EGLConfig chooseConfig(EGL10 egl, EGLDisplay display) {
            int attribs[] = { EGL10.EGL_LEVEL, 0, EGL10.EGL_RENDERABLE_TYPE, 4, // EGL_OPENGL_ES2_BIT
                    EGL10.EGL_COLOR_BUFFER_TYPE, EGL10.EGL_RGB_BUFFER, EGL10.EGL_RED_SIZE, 8, EGL10.EGL_GREEN_SIZE, 8,
                    EGL10.EGL_BLUE_SIZE, 8, EGL10.EGL_ALPHA_SIZE, 8, EGL10.EGL_DEPTH_SIZE, 24, EGL10.EGL_SAMPLE_BUFFERS,
                    1, EGL10.EGL_SAMPLES, 4, // This is for 4x MSAA.
                    EGL10.EGL_NONE };
            EGLConfig[] configs = new EGLConfig[1];
            int[] configCounts = new int[1];
            egl.eglChooseConfig(display, attribs, configs, 1, configCounts);

            if (configCounts[0] == 0) {
                // Failed! Error handling.
                return null;
            } else {
                return configs[0];
            }
        }
    }

}
