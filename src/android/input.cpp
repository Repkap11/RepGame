#include "common/RepGame.hpp"

void input_lookMove( InputState *inputState, int x, int y ) {
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
}
void input_positionHMove( InputState *inputState, float sizeH, float angleH ) {
    inputState->movement.sizeH = sizeH;
    inputState->movement.angleH = angleH;
}

void input_positionVMove( InputState *inputState, int sizeV ) {
    inputState->movement.sizeV = sizeV;
}

void input_setButtonState( InputState *inputState, int left, int middle, int right ) {
    inputState->mouse.buttons.left = left;
    inputState->mouse.buttons.middle = middle;
    inputState->mouse.buttons.right = right;
}
