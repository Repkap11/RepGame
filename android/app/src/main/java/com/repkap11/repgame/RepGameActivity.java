package com.repkap11.repgame;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.ConfigurationInfo;
import android.opengl.GLSurfaceView;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Toast;

public class RepGameActivity extends AppCompatActivity implements View.OnTouchListener {

    private static final String TAG = RepGameActivity.class.getSimpleName();
    private GLSurfaceView glSurfaceView;
    private boolean mRendererSet;
    private RepGameAndroidRenderer mRenderWrapper;
    private int mPreviousX;
    private int mPreviousY;

    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        ActivityManager activityManager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ConfigurationInfo configurationInfo = activityManager.getDeviceConfigurationInfo();

        final boolean supportsEs3 = configurationInfo.reqGlEsVersion >= 0x30001;

        if (supportsEs3) {
            glSurfaceView = new GLSurfaceView(this);
            glSurfaceView.setOnTouchListener(this);
            glSurfaceView.getKeepScreenOn();
            glSurfaceView.setEGLContextClientVersion(3);
            mRenderWrapper = new RepGameAndroidRenderer(getApplicationContext());
            glSurfaceView.setRenderer(mRenderWrapper);
            mRendererSet = true;
            setContentView(glSurfaceView);
        } else {
            // Should never be seen in production, since the manifest filters
            // unsupported devices.
            Toast.makeText(this, "This device does not support OpenGL ES 3.1.", Toast.LENGTH_LONG).show();
            return;
        }
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
        if (mRendererSet) {
            glSurfaceView.onResume();
        }
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        int x = (int) event.getX();
        int y = (int) event.getY();
        switch (event.getAction()) {
        case MotionEvent.ACTION_MOVE:
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_CANCEL:
            mRenderWrapper.onMouseInput(x - mPreviousX, y - mPreviousY);
            break;
        case MotionEvent.ACTION_DOWN:
            mRenderWrapper.onMouseInput(0, 0);
        }
        mPreviousX = x;
        mPreviousY = y;

        return true;
    }
}
