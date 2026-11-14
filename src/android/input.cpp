#include "common/RepGame.hpp"

// positionHMove / setJumpPressed / setButtonState / onInventoryClicked
// moved to the shared src/common/input_touch.cpp — the C++ touch
// controls and WASM use the same implementations.

void Input::lookMove( int x, int y ) {
    this->mouse.currentPosition.x += x;
    this->mouse.currentPosition.y += y;
}

void Input::text_input_begin( ) {
    this->text_input_active = true;
    this->text_chars_len = 0;
    this->text_backspaces = 0;
    this->text_enters = 0;
    this->text_ups = 0;
    this->text_downs = 0;
    this->text_escape = false;
    // Drop held touch controls (joystick, jump, mouse buttons) so they
    // don't keep feeding gameplay input while the chat box is up —
    // the SDL platforms do the same for held keys in their version.
    this->movement.sizeH = 0.0f;
    this->movement.angleH = 0.0f;
    this->movement.jumpPressed = false;
    this->setButtonState( 0, 0, 0 );
}

void Input::text_input_end( ) {
    this->text_input_active = false;
}

void Input::onMenuClicked( ) {
    if ( this->inventory_open ) {
        this->inventory_open = false;
    } else {
        this->menu_open = !this->menu_open;
        if ( this->menu_open ) {
            // Drop any held controls (joystick, jump, mouse buttons) so they
            // don't keep feeding gameplay input while the menu is up.
            // sneakPressed is a UI toggle mirrored by Java; leave it alone.
            this->movement.sizeH = 0.0f;
            this->movement.angleH = 0.0f;
            this->movement.jumpPressed = false;
            this->setButtonState( 0, 0, 0 );
        }
    }
}
