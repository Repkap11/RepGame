#include "common/RepGame.hpp"

static bool front, back, left, right, up, down;

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

void Input::mouseInput( const int button, const int state ) {
    switch ( button ) {
        case SDL_BUTTON_LEFT:
            this->mouse.buttons.left = !state;
            this->mouse.buttons.left_click_handled = state;
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
        default:
            break;
    }
}
void Input::mouseWheel( const int x_delta, const int y_delta ) {
    this->mouse.currentPosition.wheel_counts += y_delta;
}

void Input::quit( ) {
    this->exitGame = true;
}

void Input::keysInput( const SDL_Keycode key, const bool pressed ) {

    switch ( key ) {
        case 'q':
            this->drop_item = true;
            break;
        case SDLK_ESCAPE: // escape
            this->exitGame = true;
            break;

        case 'w':
        case 'o':
        case SDLK_UP:

            front = pressed;

            break;

        case 'a':
        case 'k':
        case SDLK_LEFT:

            left = pressed;

            break;

        case 's':
        case 'l':
        case SDLK_DOWN:

            back = pressed;

            break;

        case 'd':
        case ';':
        case SDLK_RIGHT:

            right = pressed;

            break;

        case 'p':
            this->player_sprinting = pressed;
            break;

        case ' ':
            if ( pressed ) {
                up = true;
            } else {
                up = false;
            }

            break;

        case '\'':
            down = pressed;

            break;

        case 'f':
            if ( pressed ) {
                this->player_flying = !this->player_flying;
            }
            break;
        case 'c':
            if ( pressed ) {
                if ( SDL_GetModState( ) & SDL_KMOD_CTRL ) {
                    // ctrl-c exits
                    this->exitGame = true;
                } else {
                    this->no_clip = !this->no_clip;
                }
            }
            break;
        case 'm':
            if ( pressed ) {
                this->debug_mode = !this->debug_mode;
            }
            break;
        case 'i':
        case 'e':
            if ( pressed ) {
                this->inventory_open = !this->inventory_open;
            }
            break;
        case 'r':
            if ( pressed ) {
                if ( SDL_GetModState( ) & SDL_KMOD_SHIFT ) {
                    this->worldDrawQuality = static_cast<WorldDrawQuality>( ( this->worldDrawQuality - 1 + WorldDrawQuality::LAST ) % WorldDrawQuality::LAST );
                } else {
                    this->worldDrawQuality = static_cast<WorldDrawQuality>( ( this->worldDrawQuality + 1 ) % WorldDrawQuality::LAST );
                }
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

void Input::lookMove( const int x, const int y ) {
    // pr_debug( "Using location %d %d", x, y );
    this->mouse.currentPosition.x += x;
    this->mouse.currentPosition.y += y;
}

void Input::mousePosition( const int x, const int y ) {
    this->mouse.absPosition.x = x;
    this->mouse.absPosition.y = y;
}
