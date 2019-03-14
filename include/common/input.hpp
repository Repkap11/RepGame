#ifndef HEADER_INPUT_H
#define HEADER_INPUT_H

typedef struct {
    struct {
        float sizeH;
        float angleH;
        int sizeV;
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
        } currentPosition;
        struct {
            int x;
            int y;
        } previousPosition;
    } mouse;
    int exitGame;
    int limit_fps;
    int click_delay_left;
    int click_delay_right;
    int debug_mode;
} InputState;

#if defined( REPGAME_LINUX ) || defined( REPGAME_WASM )
void input_arrowKeyDownInput( InputState *inputState, int key, int x, int y );
void input_arrowKeyUpInput( InputState *inputState, int key, int x, int y );
void input_mouseInput( InputState *inputState, int button, int state, int x, int y );
void input_set_enable_mouse( int enable );
void input_keysInput( InputState *inputState, unsigned char key, int x, int y, int pressed );
#else
void input_positionHMove( InputState *inputState, float sizeH, float angleH );
void input_positionVMove( InputState *inputState, int sizeV );
void input_setButtonState( InputState *inputState, int left, int middle, int right );
#endif
void input_lookMove( InputState *inputState, int x, int y );

#endif