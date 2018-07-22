package com.repkap11.repgame;

/**
 * Created by paul on 10/22/17.
 */

class RepGameJNIWrapper {
    public static native void onSurfaceCreated(byte[] textures);
    public static native void onDrawFrame();
    public static native void onSizeChanged(int width, int height);
    public static native void onMouseInput(int xdiff, int ydiff);
    static {
        System.loadLibrary("RepGameAndroid");
    }
}
