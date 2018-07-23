#include "RepGame.h"

void input_lookMove( InputState *inputState, int x, int y ) {
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
}
void input_positionMove( InputState *inputState, float sizeH, int sizeV, float angleH ) {
    inputState->movement.sizeH = sizeH;
    inputState->movement.sizeV = sizeV;
    inputState->movement.angleH = angleH;
}
