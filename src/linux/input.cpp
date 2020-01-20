#include "common/RepGame.hpp"

int front, back, left, right, up, down;

void processMovement( InputState *inputState ) {
    float angleH = 0;
    int sizeH = 0;
    int jumpPressed = false;
    int sneakPressed = false;

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
        jumpPressed = true;
    }
    if ( down ) {
        sneakPressed = true;
    }
    inputState->movement.angleH = angleH;
    inputState->movement.jumpPressed = jumpPressed;
    inputState->movement.sneakPressed = sneakPressed;
    inputState->movement.sizeH = sizeH;
}

void input_arrowKeyDownInput( InputState *inputState, int key, int x, int y ) {
    // switch ( key ) {
    //     case GLUT_KEY_UP:
    //         front = 1;
    //         // pr_debug( "Up Arrow 1 %d %d", x, y );
    //         break;
    //     case GLUT_KEY_DOWN:
    //         back = 1;
    //         // pr_debug( "Down Arrow 1 %d %d", x, y );
    //         break;
    //     case GLUT_KEY_LEFT:
    //         left = 1;
    //         // pr_debug( "Left Arrow 1 %d %d", x, y );
    //         break;
    //     case GLUT_KEY_RIGHT:
    //         right = 1;
    //         // pr_debug( "Right Arrow 1 %d %d", x, y );
    //         break;
    //     case 114: // Left Control
    //     case 115: // Right Control
    //         down = 1;
    //         // pr_debug( "Right Arrow 0 %d %d", x, y );
    //         break;
    //     default:
    //         pr_debug( "Got Unhandled Special Key Down: %d", key );
    //         break;
    // }
    processMovement( inputState );
}

void input_arrowKeyUpInput( InputState *inputState, int key, int x, int y ) {
    // switch ( key ) {

    //     case 114: // Left Control
    //     case 115: // Right Control
    //         down = 0;
    //         // pr_debug( "Right Arrow 0 %d %d", x, y );
    //         break;
    //     default:
    //         pr_debug( "Got Unhandled Special Key Up: %d", key );
    //         break;
    // }
    processMovement( inputState );
}

void input_mouseInput( InputState *inputState, int button, int state, int x, int y ) {
    // switch ( button ) {
    //     case GLUT_LEFT_BUTTON:
    //         inputState->mouse.buttons.left = !state;
    //         // pr_debug( "Left Click %d", !state );
    //         break;
    //     case GLUT_RIGHT_BUTTON:
    //         inputState->mouse.buttons.right = !state;
    //         // pr_debug( "Right Click %d", !state );
    //         break;
    //     case GLUT_MIDDLE_BUTTON:
    //         inputState->mouse.buttons.middle = !state;
    //         // pr_debug( "Middle Click %d", !state );
    //         break;
    //     case 3: // Up
    //         inputState->mouse.currentPosition.wheel_counts += 1;
    //         break;

    //     case 4: // Down
    //         inputState->mouse.currentPosition.wheel_counts -= 1;

    //         break;

    //         pr_debug( "Other mouse %d", button );
    // }
}

void input_keysInput( InputState *inputState, SDL_Keycode key, int pressed ) {

    switch ( key ) {
        case 'q':
        case SDLK_ESCAPE: // escape
            inputState->exitGame = 1;
            break;

        case 'w':
        case 'o':
        case SDLK_UP:

            if ( pressed ) {
                front = 1;
            } else {
                front = 0;
            }

            break;

        case 'a':
        case 'k':
        case SDLK_LEFT:

            if ( pressed ) {
                left = 1;
            } else {
                left = 0;
            }

            break;

        case 's':
        case 'l':
        case SDLK_DOWN:

            if ( pressed ) {
                back = 1;
            } else {
                back = 0;
            }

            break;

        case 'd':
        case ';':
        case SDLK_RIGHT:

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
        case 'm':
            if ( pressed ) {
                inputState->debug_mode = !inputState->debug_mode;
            }
            break;
        case 'i':
            if ( pressed ) {
                inputState->inventory_open = !inputState->inventory_open;
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