package com.repkap11.repgame;

/**
 * Created by paul on 10/22/17.
 */

class RepGameJNIWrapper {
    public static native void onSurfaceCreated();
    public static native void onDrawFrame();
    public static native void onSurfaceChanged();
    static {
        System.loadLibrary("RepGameAndroid");
    }
}
