#include <GL/freeglut.h>

#include "RepGame.h"
#include "input.h"

void input_arrowKeyDownInput( InputState *inputState, int key, int x, int y ) {
    switch ( key ) {
        case GLUT_KEY_UP:
            inputState->arrows.up = 1;
            pr_debug( "Up Arrow 1 %d %d", x, y );
            break;
        case GLUT_KEY_DOWN:
            inputState->arrows.down = 1;
            pr_debug( "Down Arrow 1 %d %d", x, y );
            break;
        case GLUT_KEY_LEFT:
            inputState->arrows.left = 1;
            pr_debug( "Left Arrow 1 %d %d", x, y );
            break;
        case GLUT_KEY_RIGHT:
            inputState->arrows.right = 1;
            pr_debug( "Right Arrow 1 %d %d", x, y );
            break;
    }
}

void input_arrowKeyUpInput( InputState *inputState, int key, int x, int y ) {
    switch ( key ) {
        case GLUT_KEY_UP:
            inputState->arrows.up = 0;
            pr_debug( "Up Arrow 0 %d %d", x, y );
            break;
        case GLUT_KEY_DOWN:
            inputState->arrows.down = 0;
            pr_debug( "Down Arrow 0 %d %d", x, y );
            break;
        case GLUT_KEY_LEFT:
            inputState->arrows.left = 0;
            pr_debug( "Left Arrow 0 %d %d", x, y );
            break;
        case GLUT_KEY_RIGHT:
            inputState->arrows.right = 0;
            pr_debug( "Right Arrow 0 %d %d", x, y );
            break;
    }
}

void input_mouseInput( InputState *inputState, int button, int state, int x, int y ) {
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
    inputState->mouse.previousPosition.x = x;
    inputState->mouse.previousPosition.y = y;
    switch ( button ) {
        case GLUT_LEFT_BUTTON:
            inputState->mouse.buttons.left = !state;
            pr_debug( "Left Click %d", !state );
            break;
        case GLUT_RIGHT_BUTTON:
            inputState->mouse.buttons.right = !state;
            pr_debug( "Right Click %d", !state );
            break;
        case GLUT_MIDDLE_BUTTON:
            inputState->mouse.buttons.middle = !state;
            pr_debug( "Middle Click %d", !state );
    }
}

void input_keysInput( InputState *inputState, unsigned char key, int x, int y, int pressed ) {

    switch ( key ) {
        case 27:  // lowercase q
        case 81:  // uppercase q
        case 113: // escape
            inputState->exitGame = 1;
            break;

        case 119:
            if ( pressed ) {
                inputState->arrows.up = 1;
            } else {
                inputState->arrows.up = 0;
            }

            break;

        case 97:
            if ( pressed ) {
                inputState->arrows.left = 1;
            } else {
                inputState->arrows.left = 0;
            }

            break;

        case 115:
            if ( pressed ) {
                inputState->arrows.down = 1;
            } else {
                inputState->arrows.down = 0;
            }

            break;

        case 100:
            if ( pressed ) {
                inputState->arrows.right = 1;
            } else {
                inputState->arrows.right = 0;
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
}

void input_mouseMove( InputState *inputState, int x, int y ) {
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
}