package com.repkap11.repgame;

/**
 * Created by paul on 10/22/17.
 */

class RepGameJNIWrapper {
    static {
        System.loadLibrary("RepGameAndroid");
    }

    public static native void onSurfaceCreated(byte[] textures);

    public static native void onDrawFrame();

    public static native void onSizeChanged(int width, int height);

    public static native void lookInput(int xdiff, int ydiff);

    public static native void positionHInput(float sizeH, float angleH);
    public static native void positionVInput(int sizeV);
}
