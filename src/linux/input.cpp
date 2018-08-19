#include "RepGame.h"

int front, back, left, right, up, down;

void processMovement( InputState *inputState ) {
    float angleH = 0;
    int sizeH = 0;
    int sizeV = 0;

    if ( front ) {
        sizeH = 1;
        if ( left && !right ) {
            angleH -= 45;
        }
        if ( right && !left ) {
            angleH += 45;
        }
        if ( !right && !left ) {
        }
    }
    if ( back ) {
        sizeH = 1;
        if ( left && !right ) {
            angleH -= 135;
        }
        if ( right && !left ) {
            angleH += 135;
        }
        if ( !right && !left ) {
            angleH += 180;
        }
    }
    if ( !front && !back ) {
        if ( left && !right ) {
            sizeH = 1;
            angleH -= 90;
        }
        if ( right && !left ) {
            sizeH = 1;
            angleH += 90;
        }
    }
    if ( up ) {
        sizeV += 1;
    } else if ( !down ) {
        sizeV -= GRAVITY_STRENGTH;
    }
    if ( down ) {
        sizeV -= 1;
    }
    inputState->movement.angleH = angleH;
    inputState->movement.sizeV = sizeV;
    inputState->movement.sizeH = sizeH;
}

void input_arrowKeyDownInput( InputState *inputState, int key, int x, int y ) {
    switch ( key ) {
        case GLUT_KEY_UP:
            front = 1;
            // pr_debug( "Up Arrow 1 %d %d", x, y );
            break;
        case GLUT_KEY_DOWN:
            back = 1;
            // pr_debug( "Down Arrow 1 %d %d", x, y );
            break;
        case GLUT_KEY_LEFT:
            left = 1;
            // pr_debug( "Left Arrow 1 %d %d", x, y );
            break;
        case GLUT_KEY_RIGHT:
            right = 1;
            // pr_debug( "Right Arrow 1 %d %d", x, y );
            break;
        case 114: // Left Control
        case 115: // Right Control
            down = 1;
            // pr_debug( "Right Arrow 0 %d %d", x, y );
            break;
        default:
            pr_debug( "Got Unhandled Special Key Down: %d", key );
            break;
    }
    processMovement( inputState );
}

void input_arrowKeyUpInput( InputState *inputState, int key, int x, int y ) {
    switch ( key ) {
        case GLUT_KEY_UP:
            front = 0;
            // pr_debug( "Up Arrow 0 %d %d", x, y );
            break;
        case GLUT_KEY_DOWN:
            back = 0;
            // pr_debug( "Down Arrow 0 %d %d", x, y );
            break;
        case GLUT_KEY_LEFT:
            left = 0;
            // pr_debug( "Left Arrow 0 %d %d", x, y );
            break;
        case GLUT_KEY_RIGHT:
            right = 0;
            // pr_debug( "Right Arrow 0 %d %d", x, y );
            break;

        case 114: // Left Control
        case 115: // Right Control
            down = 0;
            // pr_debug( "Right Arrow 0 %d %d", x, y );
            break;
        default:
            pr_debug( "Got Unhandled Special Key Up: %d", key );
            break;
    }
    processMovement( inputState );
}

void input_mouseInput( InputState *inputState, int button, int state, int x, int y ) {
    // inputState->mouse.currentPosition.x = x;
    // inputState->mouse.currentPosition.y = y;
    // inputState->mouse.previousPosition.x = x;
    // inputState->mouse.previousPosition.y = y;
    switch ( button ) {
        case GLUT_LEFT_BUTTON:
            inputState->mouse.buttons.left = !state;
            // pr_debug( "Left Click %d", !state );
            break;
        case GLUT_RIGHT_BUTTON:
            inputState->mouse.buttons.right = !state;
            // pr_debug( "Right Click %d", !state );
            break;
        case GLUT_MIDDLE_BUTTON:
            inputState->mouse.buttons.middle = !state;
            // pr_debug( "Middle Click %d", !state );
    }
}

void input_keysInput( InputState *inputState, unsigned char key, int x, int y, int pressed ) {

    switch ( key ) {
        case 27:  // lowercase q
        case 81:  // uppercase q
        case 113: // escape
            inputState->exitGame = 1;
            break;

        case 'w':
        case 'o':
            if ( pressed ) {
                front = 1;
            } else {
                front = 0;
            }

            break;

        case 'a':
        case 'k':
            if ( pressed ) {
                left = 1;
            } else {
                left = 0;
            }

            break;

        case 's':
        case 'l':

            if ( pressed ) {
                back = 1;
            } else {
                back = 0;
            }

            break;

        case 'd':
        case ';':

            if ( pressed ) {
                right = 1;
            } else {
                right = 0;
            }

            break;

        case ' ':
            if ( pressed ) {
                up = 1;
            } else {
                up = 0;
            }

            break;

        case '\'':
            if ( pressed ) {
                down = 1;
            } else {
                down = 0;
            }

            break;

        case 'f':
            if ( pressed ) {
                inputState->limit_fps = !inputState->limit_fps;
            }

            break;

        default:
            if ( pressed ) {
                pr_debug( "Got Unhandled Key Down: %d", key );

            } else {
                pr_debug( "Got Unhandled Key Up: %d", key );
            }
            break;
    }
    processMovement( inputState );
}

void input_lookMove( InputState *inputState, int x, int y ) {
    // pr_debug( "Using location %d %d", x, y );
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
}