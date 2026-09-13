package com.repkap11.repgame;

import android.content.Context;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.util.Log;

import java.io.InputStream;
import java.io.File;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.opengles.GL10;

/**
 * Created by paul on 10/22/17.
 */

public class RepGameAndroidRenderer implements GLSurfaceView.Renderer {
    static private final String TAG = RepGameAndroidRenderer.class.getSimpleName();
    private final Context mApplicationContext;

    public RepGameAndroidRenderer(Context applicationContext) {
        mApplicationContext = applicationContext;
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
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
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        RepGameJNIWrapper.onSizeChanged(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        RepGameJNIWrapper.onDrawFrame();
    }

    public void lookInput(int xdiff, int ydiff) {
        RepGameJNIWrapper.lookInput(xdiff, ydiff);
    }

    public void positionHInput(float sizeH, float angleH) {
        RepGameJNIWrapper.positionHInput(sizeH, angleH);
    }

    public void setButtonState(int left, int middle, int right) {
        RepGameJNIWrapper.setButtonState(left, middle, right);
    }


    public void setJumpPressed(int jumpPressed) {
        RepGameJNIWrapper.setJumpPressed(jumpPressed);
    }

    public void onInventoryClicked() {
        RepGameJNIWrapper.onInventoryClicked();
    }


    public void onDestroy() {
        RepGameJNIWrapper.onSurfaceDestroyed();
    }

    public static class ConfigChooser implements GLSurfaceView.EGLConfigChooser {
        @Override
        public EGLConfig chooseConfig(EGL10 egl, EGLDisplay display) {
            // Try 2x MSAA first; fall back to no MSAA if the device doesn't
            // support it. Android is always in LOW quality (no FBO path),
            // so MSAA on the default framebuffer is the only edge AA.
            // ImGui is not used on Android, so there's no glitching concern.
            EGLConfig config = chooseConfigWithSamples(egl, display, 2);
            if (config == null) {
                Log.w(TAG, "2x MSAA not available, falling back to no MSAA");
                config = chooseConfigWithSamples(egl, display, 0);
            }
            return config;
        }

        private EGLConfig chooseConfigWithSamples(EGL10 egl, EGLDisplay display, int samples) {
            int attribs[] = {EGL10.EGL_LEVEL, 0,//
                    EGL10.EGL_RENDERABLE_TYPE, 4, // EGL_OPENGL_ES2_BIT
                    EGL10.EGL_COLOR_BUFFER_TYPE, EGL10.EGL_RGB_BUFFER, //
                    EGL10.EGL_RED_SIZE, 8, //
                    EGL10.EGL_GREEN_SIZE, 8,//
                    EGL10.EGL_BLUE_SIZE, 8,//
                    EGL10.EGL_ALPHA_SIZE, 8,//
                    EGL10.EGL_DEPTH_SIZE, 24, //
                    EGL10.EGL_SAMPLE_BUFFERS, samples > 0 ? 1 : 0, //
                    EGL10.EGL_SAMPLES, samples, //
                    EGL10.EGL_NONE};
            EGLConfig[] configs = new EGLConfig[1];
            int[] configCounts = new int[1];
            egl.eglChooseConfig(display, attribs, configs, 1, configCounts);

            if (configCounts[0] == 0) {
                return null;
            } else {
                return configs[0];
            }
        }
    }

}
