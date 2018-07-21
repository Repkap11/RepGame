package com.repkap11.repgame;
import static android.content.ContentValues.TAG;
import static android.opengl.GLES20.*;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLSurfaceView;
import android.util.Log;

import java.io.InputStream;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Created by paul on 10/22/17.
 */

class RendererWrapper implements GLSurfaceView.Renderer {
    private final Context mApplicationContext;

    public RendererWrapper(Context applicationContext) {
        mApplicationContext = applicationContext;
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        InputStream textureIS = mApplicationContext.getResources().openRawResource(R.raw.textures);
        byte[] targetArray = null;
        try {
            targetArray = new byte[textureIS.available()];
            textureIS.read(targetArray);
        } catch( Exception e){
            Log.d(TAG, "Failed to read bitmap");
        }
        RepGameJNIWrapper.onSurfaceCreated(targetArray);
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

