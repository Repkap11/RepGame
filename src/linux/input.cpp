#include "common/RepGame.hpp"

int front, back, left, right, up, down;

void Input::processMovement( ) {
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
    this->movement.angleH = angleH;
    this->movement.jumpPressed = jumpPressed;
    this->movement.sneakPressed = sneakPressed;
    this->movement.sizeH = sizeH;
}

void Input::mouseInput( int button, int state ) {
    switch ( button ) {
        case SDL_BUTTON_LEFT:
            this->mouse.buttons.left = !state;
            // pr_debug( "Left Click %d", !state );
            break;
        case SDL_BUTTON_RIGHT:
            this->mouse.buttons.right = !state;
            // pr_debug( "Right Click %d", !state );
            break;
        case SDL_BUTTON_MIDDLE:
            this->mouse.buttons.middle = !state;
            // pr_debug( "Middle Click %d", !state );
            break;

            pr_debug( "Other mouse %d", button );
    }
}
void Input::mouseWheel( int x_delta, int y_delta ) {
    this->mouse.currentPosition.wheel_counts += y_delta;
}

void Input::quit( ) {
    this->exitGame = 1;
}

void Input::keysInput( SDL_Keycode key, int pressed ) {

    switch ( key ) {
        case 'q':
            this->drop_item = true;
            break;
        case SDLK_ESCAPE: // escape
            this->exitGame = 1;
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
            this->player_sprinting = pressed;
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
                this->player_flying = !this->player_flying;
            }
            break;
        case 'c':
            if ( pressed ) {
                this->no_clip = !this->no_clip;
            }

            break;
        case 'm':
            if ( pressed ) {
                this->debug_mode = !this->debug_mode;
            }
            break;
        case 'i':
            if ( pressed ) {
                this->inventory_open = !this->inventory_open;
            }
            break;
        case 'r':
            if ( pressed ) {
                this->reflections_on = !this->reflections_on;
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
    processMovement( );
}

void Input::lookMove( int x, int y ) {
    // pr_debug( "Using location %d %d", x, y );
    this->mouse.currentPosition.x += x;
    this->mouse.currentPosition.y += y;
}