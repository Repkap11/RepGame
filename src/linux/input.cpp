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

void input_mouseInput( InputState *inputState, int button, int state, int x, int y ) {
    inputState->mouse.buttons.click_pos_x = x;
    inputState->mouse.buttons.click_pos_y = y;
    switch ( button ) {
        case SDL_BUTTON_LEFT:
            inputState->mouse.buttons.left = !state;
            // pr_debug( "Left Click %d", !state );
            break;
        case SDL_BUTTON_RIGHT:
            inputState->mouse.buttons.right = !state;
            // pr_debug( "Right Click %d", !state );
            break;
        case SDL_BUTTON_MIDDLE:
            inputState->mouse.buttons.middle = !state;
            // pr_debug( "Middle Click %d", !state );
            break;

            pr_debug( "Other mouse %d", button );
    }
}
void input_mouseWheel( InputState *inputState, int x_delta, int y_delta ) {
    inputState->mouse.currentPosition.wheel_counts += y_delta;
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

        case 'p':
            inputState->player_sprinting = pressed;
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
                inputState->player_flying = !inputState->player_flying;
            }
            break;
        case 'c':
            if ( pressed ) {
                inputState->no_clip = !inputState->no_clip;
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
        case 'r':
            if ( pressed ) {
                inputState->reflections_on = !inputState->reflections_on;
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
    inputState->mouse.currentPosition.x += x;
    inputState->mouse.currentPosition.y += y;
}