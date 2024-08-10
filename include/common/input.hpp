#pragma once

class Input {
    void processMovement( );

  public:
    struct {
        float sizeH;
        float angleH;
        bool jumpPressed;
        bool sneakPressed;
    } movement;
    struct {
        struct {
            int left;
            int right;
            int middle;
        } buttons;
        struct {
            int x;
            int y;
            int wheel_counts;
        } currentPosition;
        struct {
            int x;
            int y;
            int wheel_counts;
        } previousPosition;
    } mouse;
    bool exitGame;
    int click_delay_left;
    int click_delay_right;
    bool debug_mode;
    bool inventory_open;
    bool player_flying;
    bool player_sprinting;
    bool no_clip;
    WorldDrawQuality worldDrawQuality;
    bool drop_item;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM )
    void mouseInput( int button, int state );
    void set_enable_mouse( int enable );
    void keysInput( SDL_Keycode key, int pressed );
    void mouseWheel( int x_delta, int y_delta );
    void quit( );
#else
    void positionHMove( float sizeH, float angleH );
    void setJumpPressed( int jumpPressed );
    void setButtonState( int left, int middle, int right );
    void onInventoryClicked( );
#endif
    void lookMove( int x, int y );
};