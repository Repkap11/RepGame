package com.repkap11.repgame;

import android.content.Context;
import android.opengl.GLSurfaceView;

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
    private final RepGameActivity mActivity;
    private boolean mExitHandled = false;

    public RepGameAndroidRenderer(Context applicationContext, RepGameActivity activity) {
        mApplicationContext = applicationContext;
        mActivity = activity;
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        RepGameJNIWrapper.onSurfaceCreated(mApplicationContext.getFilesDir().getPath() + File.separator + "World1");
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        RepGameJNIWrapper.onSizeChanged(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        RepGameJNIWrapper.onDrawFrame();
        // The game asked to exit (the pause menu's Exit Game): finish the
        // activity once. finish() must run on the UI thread; the teardown
        // it triggers (onDestroy -> onSurfaceDestroyed -> repgame.cleanup)
        // saves the world.
        if (!mExitHandled && RepGameJNIWrapper.shouldExit()) {
            mExitHandled = true;
            mActivity.runOnUiThread(mActivity::finish);
        }
    }

    public void onDestroy() {
        RepGameJNIWrapper.onSurfaceDestroyed();
    }

    public static class ConfigChooser implements GLSurfaceView.EGLConfigChooser {
        @Override
        public EGLConfig chooseConfig(EGL10 egl, EGLDisplay display) {
            // No MSAA on Android. The device was upgrading 2x requests to 4x,
            // and 4x MSAA quadruples fragment-shading cost on tile-based mobile
            // GPUs — the GPU is the frame bottleneck (profiling showed ~94 FPS
            // with GPU-bound frames). Disabling MSAA recovers that cost. Edge
            // aliasing is acceptable on a small mobile screen, and the game
            // uses alpha-tested foliage (not alpha-to-coverage) so MSAA wasn't
            // smoothing foliage edges anyway.
            EGLConfig config = chooseConfigWithSamples(egl, display, 0);
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
