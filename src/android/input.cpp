#include "common/RepGame.hpp"

void Input::lookMove( int x, int y ) {
    this->mouse.currentPosition.x += x;
    this->mouse.currentPosition.y += y;
}
void Input::positionHMove( float sizeH, float angleH ) {
    this->movement.sizeH = sizeH;
    this->movement.angleH = angleH;
}

void Input::setJumpPressed( int jumpPressed ) {
    this->movement.jumpPressed = jumpPressed;
}

void Input::onInventoryClicked( ) {
    this->inventory_open = !this->inventory_open;
}

void Input::setButtonState( int left, int middle, int right ) {
    // Record each press's source on the rising edge and clear it on release,
    // mirroring Input::mouseInput on the SDL platforms.
    if ( left && !this->mouse.buttons.left ) {
        this->mouse.buttons.left_pressed_in_ui = this->inventory_open;
    } else if ( !left ) {
        this->mouse.buttons.left_pressed_in_ui = false;
    }
    if ( middle && !this->mouse.buttons.middle ) {
        this->mouse.buttons.middle_pressed_in_ui = this->inventory_open;
    } else if ( !middle ) {
        this->mouse.buttons.middle_pressed_in_ui = false;
    }
    if ( right && !this->mouse.buttons.right ) {
        this->mouse.buttons.right_pressed_in_ui = this->inventory_open;
    } else if ( !right ) {
        this->mouse.buttons.right_pressed_in_ui = false;
    }
    this->mouse.buttons.left = left;
    this->mouse.buttons.middle = middle;
    this->mouse.buttons.right = right;
}
