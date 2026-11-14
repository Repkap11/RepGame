// Touch-driven Input methods — historically Android-only (src/android),
// now shared so the C++ touch controls (touch_controls.cpp) and WASM can
// drive them. The SDL platforms keep their own mouse/key paths.

#include "common/RepGame.hpp"

void Input::positionHMove( float sizeH, float angleH ) {
    this->movement.sizeH = sizeH;
    this->movement.angleH = angleH;
}

void Input::setJumpPressed( int jumpPressed ) {
    this->movement.jumpPressed = jumpPressed;
}

void Input::onInventoryClicked( ) {
    // The pause menu owns the screen; the inventory can't open under it.
    if ( this->menu_open ) {
        return;
    }
    this->inventory_open = !this->inventory_open;
}

void Input::backPressed( ) {
    // Dismiss a text field before treating back as Escape: the soft
    // keyboard's capture ends (sign edit / chat drain text_escape,
    // search drains search_close), otherwise the pause-menu toggle runs.
    if ( this->text_input_active ) {
        this->text_escape = true;
    } else if ( this->search_focused ) {
        this->search_close = true;
    } else {
        this->onMenuClicked( );
    }
}

void Input::setButtonState( int left, int middle, int right ) {
    // Record each press's source on the rising edge and clear it on
    // release, mirroring Input::mouseInput on the SDL platforms.
    const bool in_ui = this->inventory_open || this->menu_open || this->text_input_active;
    if ( left && !this->mouse.buttons.left ) {
        this->mouse.buttons.left_pressed_in_ui = in_ui;
    } else if ( !left ) {
        this->mouse.buttons.left_pressed_in_ui = false;
    }
    if ( middle && !this->mouse.buttons.middle ) {
        this->mouse.buttons.middle_pressed_in_ui = in_ui;
    } else if ( !middle ) {
        this->mouse.buttons.middle_pressed_in_ui = false;
    }
    if ( right && !this->mouse.buttons.right ) {
        this->mouse.buttons.right_pressed_in_ui = in_ui;
    } else if ( !right ) {
        this->mouse.buttons.right_pressed_in_ui = false;
    }
    this->mouse.buttons.left = left;
    this->mouse.buttons.middle = middle;
    this->mouse.buttons.right = right;
}
