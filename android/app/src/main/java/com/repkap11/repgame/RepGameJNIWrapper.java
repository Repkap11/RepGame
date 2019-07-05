package com.repkap11.repgame;

import android.content.res.AssetManager;

/**
 * Created by paul on 10/22/17.
 */

class RepGameJNIWrapper {
    static {
        System.loadLibrary("RepGameAndroid");
    }

    public static native void onSurfaceCreated(byte[] textureBlocks, byte[] textureSky, AssetManager assetManager, String world_name);

    public static native void onSurfaceDestroyed();

    public static native void onDrawFrame();

    public static native void onSizeChanged(int width, int height);

    public static native void lookInput(int xdiff, int ydiff);

    public static native void positionHInput(float sizeH, float angleH);

    public static native void positionVInput(int sizeV);

    public static native void setButtonState(int left, int middle, int right);

}
