package com.repkap11.repgame;

/**
 * Created by paul on 10/22/17.
 */

class RepGameJNIWrapper {
    static {
        System.loadLibrary("RepGameAndroid");
    }

    // Touch phases — must match TouchPhase in
    // include/common/touch_controls.hpp (TOUCH_DOWN..TOUCH_CANCEL).
    public static final int TOUCH_DOWN = 0;
    public static final int TOUCH_MOVE = 1;
    public static final int TOUCH_UP = 2;
    public static final int TOUCH_CANCEL = 3;

    // Returns false when the process already initialized the engine
    // once — the renderer must then restart the process (the engine is
    // init-once and cleanup() has already saved the world natively).
    public static native boolean onSurfaceCreated(String world_name);

    public static native void onSurfaceDestroyed();

    public static native void onDrawFrame();

    public static native void onSizeChanged(int width, int height);

    // Forwarded MotionEvents — the shared C++ touch controls consume
    // these. Must be called on the GL thread (via queueEvent).
    public static native void touchEvent(int phase, long id, float x, float y);

    // Android back button → Input::backPressed (dismiss text capture or
    // search first, otherwise acts like Menu/Escape).
    public static native void backPressed();

    public static native boolean shouldExit();

}
