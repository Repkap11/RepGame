#include "RepGame.h"

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
