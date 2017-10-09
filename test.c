#include <GL/freeglut.h>
#include <GL/gl.h>

#include "test.h"
#include "input.h"

void initilizeGameState(RepGameState* gameState){
    gameState->exitGame = 0;
    gameState->colorR = 1;
    gameState->colorG = 0;
    gameState->colorB = 0;
}

void display(RepGameState* gameState) {
    glClearColor(gameState->colorR, gameState->colorG, gameState->colorB, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();

}

void gameTick(RepGameState *gameState){
	if (gameState->exitGame){
		//Don't bother updating the state if the game is exiting
		return;
	}
	if (gameState->input.arrows.left){
		gameState->colorR = 1;
	} else {
		gameState->colorR = 0;
	}
}

RepGameState globalGameState;

void arrowKeyDownInput(int key, int x, int y) {
	input_arrowKeyDownInput(&globalGameState, key, x, y);
}
void arrowKeyUpInput(int key, int x, int y) {
	input_arrowKeyUpInput(&globalGameState, key, x, y);
}
void mouseInput(int button, int state, int x, int y) {
	input_mouseInput(&globalGameState, button,state, x, y);
}

int main(int argc, char**argv) {
    glutInit(&argc, argv);
    //glutInitWindowPosition(100,100);
    //glutInitWindowSize(500,500);
    glutCreateWindow("RepGame");
    glutSpecialFunc(arrowKeyDownInput);
    glutSpecialUpFunc(arrowKeyUpInput);
    glutMouseFunc(mouseInput);
    initilizeGameState(&globalGameState);
    while (!globalGameState.exitGame){
        glutMainLoopEvent();
        gameTick(&globalGameState);
        display(&globalGameState);
    }
    glutLeaveMainLoop();
    return 0;
}