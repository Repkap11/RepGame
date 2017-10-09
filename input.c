#include <GL/freeglut.h>

#include "input.h"
void input_arrowKeyDownInput(RepGameState* gameState, int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            gameState->input.arrows.up = 1;
            pr_debug("Up Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            gameState->input.arrows.down = 1;
            pr_debug("Down Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            gameState->input.arrows.left = 1;
            pr_debug("Left Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            gameState->input.arrows.right = 1;
            pr_debug("Right Arrow 1 %d %d", x, y);
        break;
    }
}
void input_arrowKeyUpInput(RepGameState* gameState, int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            gameState->input.arrows.up = 0;
            pr_debug("Up Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            gameState->input.arrows.down = 0;
            pr_debug("Down Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            gameState->input.arrows.left = 0;
            pr_debug("Left Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            gameState->input.arrows.right = 0;
            pr_debug("Right Arrow 0 %d %d", x, y);
        break;
    }
}


void input_mouseInput(RepGameState* gameState, int button, int state, int x, int y){
    switch(button) {
        case GLUT_LEFT_BUTTON:
            gameState->input.mouse.left = !state;
            pr_debug("Left Click %d", !state);
        break;
        case GLUT_RIGHT_BUTTON:
            gameState->input.mouse.right = !state;
            pr_debug("Right Click %d", !state);
        break;
        case GLUT_MIDDLE_BUTTON:
            gameState->input.mouse.middle = !state;
            pr_debug("Middle Click %d", !state);
    }
}
void input_keysInput(RepGameState* gameState, unsigned char key, int x, int y){
    pr_debug("Got Key:%d", key);
    if (key == 27 || key == 113){
        gameState->exitGame = 1;
    }
}