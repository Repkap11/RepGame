#include <GL/freeglut.h>

#include "input.h"
#include "RepGame.h"

void input_arrowKeyDownInput(InputState* inputState, int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            inputState->arrows.up = 1;
            pr_debug("Up Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            inputState->arrows.down = 1;
            pr_debug("Down Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            inputState->arrows.left = 1;
            pr_debug("Left Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            inputState->arrows.right = 1;
            pr_debug("Right Arrow 1 %d %d", x, y);
        break;
    }
}

void input_arrowKeyUpInput(InputState* inputState, int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            inputState->arrows.up = 0;
            pr_debug("Up Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            inputState->arrows.down = 0;
            pr_debug("Down Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            inputState->arrows.left = 0;
            pr_debug("Left Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            inputState->arrows.right = 0;
            pr_debug("Right Arrow 0 %d %d", x, y);
        break;
    }
}

void input_mouseInput(InputState* inputState, int button, int state, int x, int y){
    switch(button) {
        case GLUT_LEFT_BUTTON:
            inputState->mouse.buttons.left = !state;
            pr_debug("Left Click %d", !state);
        break;
        case GLUT_RIGHT_BUTTON:
            inputState->mouse.buttons.right = !state;
            if (inputState->mouse.buttons.right){
                inputState->mouse.rightPressedPosition.x = x;
                inputState->mouse.rightPressedPosition.y = y;
            }
            pr_debug("Right Click %d", !state);
        break;
        case GLUT_MIDDLE_BUTTON:
            inputState->mouse.buttons.middle = !state;
            pr_debug("Middle Click %d", !state);
    }
}

void input_keysInput(InputState* inputState, unsigned char key, int x, int y){
    pr_debug("Got Key:%d", key);
    if (key == 27 || key == 113){
        inputState->exitGame = 1;
    }
}

void input_mouseMove(InputState* inputState, int x, int y){
    inputState->mouse.currentPosition.x = x;
    inputState->mouse.currentPosition.y = y;
}