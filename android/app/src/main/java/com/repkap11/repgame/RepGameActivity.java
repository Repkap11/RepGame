package com.repkap11.repgame;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ConfigurationInfo;
import android.opengl.GLSurfaceView;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Toast;

public class RepGameActivity extends AppCompatActivity {

    private static final String TAG = RepGameActivity.class.getSimpleName();
    private GLSurfaceView glSurfaceView;
    private boolean mRendererSet;
    private RepGameAndroidRenderer mRenderWrapper;

    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_rep_game);
        configureFullScreen();

        //Configure the JNI wrapper
        mRenderWrapper = new RepGameAndroidRenderer(getApplicationContext(), this);

        //Verify that we support OpenGL ES 3.1
        ConfigurationInfo configurationInfo = ((ActivityManager) getSystemService(Context.ACTIVITY_SERVICE)).getDeviceConfigurationInfo();
        final boolean supportsEs31 = configurationInfo.reqGlEsVersion >= 0x30001;
        if (!supportsEs31) {
            // Should never be seen in production, since the manifest filters
            // unsupported devices.
            Toast.makeText(this, "This device does not support OpenGL ES 3.1.", Toast.LENGTH_LONG).show();
            return;
        }


        //Configure the Surface View
        glSurfaceView = findViewById(R.id.repgame_surfaceview);
        glSurfaceView.setKeepScreenOn(true);
        glSurfaceView.setEGLContextClientVersion(3);
        glSurfaceView.setEGLConfigChooser(new RepGameAndroidRenderer.ConfigChooser());
        glSurfaceView.setRenderer(mRenderWrapper);
        glSurfaceView.setPreserveEGLContextOnPause(true);

        mRendererSet = true;

        // The on-screen controls are drawn in-game by the shared C++
        // TouchControls — this view just forwards MotionEvents. Events are
        // dispatched via queueEvent so every Input write happens on the GL
        // thread (the old UI-overlay path wrote from the UI thread, racing
        // the render thread's tick).
        glSurfaceView.setOnTouchListener((view, event) -> {
            final int actionIndex = event.getActionIndex();
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_POINTER_DOWN: {
                    final long id = event.getPointerId(actionIndex);
                    final float x = event.getX(actionIndex);
                    final float y = event.getY(actionIndex);
                    glSurfaceView.queueEvent(() -> RepGameJNIWrapper.touchEvent(RepGameJNIWrapper.TOUCH_DOWN, id, x, y));
                    break;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_POINTER_UP: {
                    final long id = event.getPointerId(actionIndex);
                    final float x = event.getX(actionIndex);
                    final float y = event.getY(actionIndex);
                    glSurfaceView.queueEvent(() -> RepGameJNIWrapper.touchEvent(RepGameJNIWrapper.TOUCH_UP, id, x, y));
                    break;
                }
                case MotionEvent.ACTION_MOVE:
                case MotionEvent.ACTION_CANCEL: {
                    // MOVE and CANCEL cover every active pointer.
                    final int phase = event.getActionMasked() == MotionEvent.ACTION_MOVE ? RepGameJNIWrapper.TOUCH_MOVE
                                                                                       : RepGameJNIWrapper.TOUCH_CANCEL;
                    final int n = event.getPointerCount();
                    final long[] ids = new long[n];
                    final float[] xs = new float[n];
                    final float[] ys = new float[n];
                    for (int i = 0; i < n; i++) {
                        ids[i] = event.getPointerId(i);
                        xs[i] = event.getX(i);
                        ys[i] = event.getY(i);
                    }
                    glSurfaceView.queueEvent(() -> {
                        for (int i = 0; i < n; i++) {
                            RepGameJNIWrapper.touchEvent(phase, ids[i], xs[i], ys[i]);
                        }
                    });
                    break;
                }
            }
            return true;
        });
    }

    @Override
    protected void onStart() {
        super.onStart();
        configureFullScreen();
        if (mRendererSet) {
            glSurfaceView.onResume();

        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (mRendererSet) {
            glSurfaceView.onPause();
        }
    }

    @Override
    public void onBackPressed() {
        if (mRendererSet) {
            // Back is the Android Escape: it abandons a text capture or
            // search box first, otherwise closes the inventory / toggles
            // the pause menu. Exiting goes through Exit Game.
            glSurfaceView.queueEvent(() -> RepGameJNIWrapper.backPressed());
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mRendererSet) {
            mRenderWrapper.onDestroy();
        }
    }


    // The engine is init-once: when a second GL surface is created in a
    // live process the native side saves and shuts down instead, and we
    // relaunch into a fresh process so the game comes back up.
    void relaunchProcess() {
        Intent intent = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (intent != null) {
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);
        }
        Runtime.getRuntime().exit(0);
    }

    private void configureFullScreen() {
        View decorView = getWindow().getDecorView();
        int uiOptions = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
        decorView.setSystemUiVisibility(uiOptions);
    }
}
