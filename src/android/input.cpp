#include "common/RepGame.hpp"

void Input::lookMove( int x, int y ) {
    this->mouse.currentPosition.x = x;
    this->mouse.currentPosition.y = y;
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
    this->mouse.buttons.left = left;
    this->mouse.buttons.middle = middle;
    this->mouse.buttons.right = right;
}
