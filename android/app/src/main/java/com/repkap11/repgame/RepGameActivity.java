package com.repkap11.repgame;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ConfigurationInfo;
import android.opengl.GLSurfaceView;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.View;
import android.widget.Toast;

import com.repkap11.repgame.views.UIOverlayView;

public class RepGameActivity extends AppCompatActivity {

    private static final String TAG = RepGameActivity.class.getSimpleName();
    private GLSurfaceView glSurfaceView;
    private boolean mRendererSet;
    private RepGameAndroidRenderer mRenderWrapper;
    private int mPreviousX;
    private int mPreviousY;
    private UIOverlayView mUIOverlay;

    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_rep_game);
        configureFullScreen();
        {
            //Configure the JNI wrapper
            mRenderWrapper = new RepGameAndroidRenderer(getApplicationContext());
        }
        {
            //Verify that we support OpenGL ES 3.2
            ConfigurationInfo configurationInfo = ((ActivityManager) getSystemService(Context.ACTIVITY_SERVICE)).getDeviceConfigurationInfo();
            final boolean supportsEs32 = configurationInfo.reqGlEsVersion >= 0x30002;
            if (!supportsEs32) {
                // Should never be seen in production, since the manifest filters
                // unsupported devices.
                Toast.makeText(this, "This device does not support OpenGL ES 3.2.", Toast.LENGTH_LONG).show();
                return;
            }
        }
        {
            //Configure the Surface View
            glSurfaceView = findViewById(R.id.repgame_surfaceview);
            glSurfaceView.getKeepScreenOn();
            glSurfaceView.setEGLContextClientVersion(3);
            glSurfaceView.setEGLConfigChooser(new RepGameAndroidRenderer.ConfigChooser());
            glSurfaceView.setRenderer(mRenderWrapper);
            mRendererSet = true;
        }
        {
            //Configure the UI overlay
            mUIOverlay = findViewById(R.id.repgame_ui_overlay_view);
        }

        mUIOverlay.setRenderer(mRenderWrapper);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mRendererSet) {
            glSurfaceView.onPause();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        configureFullScreen();
        if (mRendererSet) {
            glSurfaceView.onResume();
        }
    }

    private void configureFullScreen() {
        View decorView = getWindow().getDecorView();
        int uiOptions = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION;
        decorView.setSystemUiVisibility(uiOptions);
    }
}
