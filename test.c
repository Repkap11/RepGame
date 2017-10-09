#include <GL/freeglut.h>
#include <GL/gl.h>
#include <stdio.h>


#define DEBUG 1

#define pr_debug(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s():"fmt"\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)


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

RepGameState gameState;

void initilizeGameState(RepGameState gameState){
    gameState.exitGame = 0;
    gameState.colorR = 1;
    gameState.colorG = 0;
    gameState.colorB = 0;
}

void arrowKeyDownInput(int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            gameState.exitGame = 1;
            gameState.input.arrows.up = 1;
            pr_debug("Up Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            gameState.input.arrows.down = 1;
            pr_debug("Down Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            gameState.input.arrows.left = 1;
            pr_debug("Left Arrow 1 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            gameState.input.arrows.right = 1;
            pr_debug("Right Arrow 1 %d %d", x, y);
        break;
    }
}
void arrowKeyUpInput(int key, int x, int y) {
    switch(key) {
        case GLUT_KEY_UP:
            gameState.exitGame = 1;
            gameState.input.arrows.up = 0;
            pr_debug("Up Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_DOWN:
            gameState.input.arrows.down = 0;
            pr_debug("Down Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_LEFT:
            gameState.input.arrows.left = 0;
            pr_debug("Left Arrow 0 %d %d", x, y);
        break;
        case GLUT_KEY_RIGHT:
            gameState.input.arrows.right = 0;
            pr_debug("Right Arrow 0 %d %d", x, y);
        break;
    }
}


void mouseInput(int button, int state, int x, int y){

    switch(button) {
        case GLUT_LEFT_BUTTON:
            gameState.input.mouse.left = !state;
            pr_debug("Left Click %d", !state);
        break;
        case GLUT_RIGHT_BUTTON:
            gameState.input.mouse.right = !state;
            pr_debug("Right Click %d", !state);
        break;
        case GLUT_MIDDLE_BUTTON:
            gameState.input.mouse.middle = !state;
            pr_debug("Middle Click %d", !state);
    }
}


void display(RepGameState gameState) {
    glClearColor(gameState.colorR, gameState.colorG, gameState.colorB, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();

}

void gameTick(RepGameState gameState){
}

int main(int argc, char**argv) {
    glutInit(&argc, argv);
    //glutInitWindowPosition(100,100);
    //glutInitWindowSize(500,500);
    glutCreateWindow("RepGame");
    //glutDisplayFunc(display);
    glutSpecialFunc(arrowKeyDownInput);
    glutSpecialUpFunc(arrowKeyUpInput);

    glutMouseFunc(mouseInput);
    initilizeGameState(gameState);
    while (!gameState.exitGame){
        glutMainLoopEvent();
        gameTick(gameState);
        display(gameState);
    }
    glutLeaveMainLoop();
    return 0;
}