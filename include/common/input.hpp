#ifndef HEADER_INPUT_H
#define HEADER_INPUT_H

typedef struct {
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
    int exitGame;
    int limit_fps;
    int click_delay_left;
    int click_delay_right;
    int debug_mode;
    int inventory_open;
} InputState;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WINDOWS ) || defined( REPGAME_WASM )
void input_mouseInput( InputState *inputState, int button, int state );
void input_set_enable_mouse( int enable );
void input_keysInput( InputState *inputState, SDL_Keycode key, int pressed );
void input_mouseWheel( InputState *inputState, int x_delta, int y_delta );
#else
void input_positionHMove( InputState *inputState, float sizeH, float angleH );
void input_setJumpPressed( InputState *inputState, int jumpPressed );
void input_setButtonState( InputState *inputState, int left, int middle, int right );
#endif
void input_lookMove( InputState *inputState, int x, int y );

#endif