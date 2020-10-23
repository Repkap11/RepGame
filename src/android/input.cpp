#include "common/RepGame.hpp"

void input_lookMove( InputState *inputState, int x, int y ) {
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
}
void input_positionHMove( InputState *inputState, float sizeH, float angleH ) {
    inputState->movement.sizeH = sizeH;
    inputState->movement.angleH = angleH;
}

void input_setJumpPressed( InputState *inputState, int jumpPressed ) {
    inputState->movement.jumpPressed = jumpPressed;
}

void input_setButtonState( InputState *inputState, int left, int middle, int right ) {
    inputState->mouse.buttons.left = left;
    inputState->mouse.buttons.middle = middle;
    inputState->mouse.buttons.right = right;
}

void input_setHeadForVR( InputState *inputState, float* headView){
    for (int i=0 ; i<16 ; i++){
        inputState->headOverride[i] = headView[i];
    }
}
