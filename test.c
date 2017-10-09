#include <GL/freeglut.h>
#include <GL/gl.h>
#include <stdio.h>


#define DEBUG 1

#define pr_debug(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s():"fmt"\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

#define test pr_debug();

typedef struct {
    int exitGame;
    int colorR;
    int colorG;
    int colorB;
    struct  {
        struct {
            int left;
            int right;
            int up;
            int down;
        } arrows;
        struct {
            int left;
            int right;
            int middle;
        } mouse;
    } input;

} RepGameState;

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
    switch(key) {
        case GLUT_KEY_UP:
            globalGameState.exitGame = 1;
            globalGameState.input.arrows.up = 1;
            pr_debug("Up Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            globalGameState.input.arrows.down = 1;
            pr_debug("Down Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            globalGameState.input.arrows.left = 1;
            pr_debug("Left Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            globalGameState.input.arrows.right = 1;
            pr_debug("Right Arrow 1 %d %d", x, y);
        break;
    }
}
void arrowKeyUpInput(int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            globalGameState.exitGame = 1;
            globalGameState.input.arrows.up = 0;
            pr_debug("Up Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            globalGameState.input.arrows.down = 0;
            pr_debug("Down Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            globalGameState.input.arrows.left = 0;
            pr_debug("Left Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            globalGameState.input.arrows.right = 0;
            pr_debug("Right Arrow 0 %d %d", x, y);
        break;
    }
}


void mouseInput(int button, int state, int x, int y){
    switch(button) {
        case GLUT_LEFT_BUTTON:
            globalGameState.input.mouse.left = !state;
            pr_debug("Left Click %d", !state);
        break;
        case GLUT_RIGHT_BUTTON:
            globalGameState.input.mouse.right = !state;
            pr_debug("Right Click %d", !state);
        break;
        case GLUT_MIDDLE_BUTTON:
            globalGameState.input.mouse.middle = !state;
            pr_debug("Middle Click %d", !state);
    }
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