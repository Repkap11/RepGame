#include <GL/freeglut.h>
#include <GL/gl.h>
#include <math.h>

#include "RepGame.h"
#include "input.h"

void initilizeGameState(RepGameState* gameState){
    gameState->exitGame = 0;
    gameState->colorR = 1;
    gameState->colorG = 0;
    gameState->colorB = 0;
    gameState->camera.angle = 0.0f;
    gameState->camera.lx = 0.0f;
    gameState->camera.lz = -1.0f;
    gameState->camera.x = 0.0f;
    gameState->camera.z = 5.0f;

}

void drawSnowMan() {

    glColor3f(1.0f, 1.0f, 1.0f);

// Draw Body
    glTranslatef(0.0f ,0.75f, 0.0f);
    glutSolidSphere(0.75f,20,20);

// Draw Head
    glTranslatef(0.0f, 1.0f, 0.0f);
    glutSolidSphere(0.25f,20,20);

// Draw Eyes
    glPushMatrix();
    glColor3f(0.0f,0.0f,0.0f);
    glTranslatef(0.05f, 0.10f, 0.18f);
    glutSolidSphere(0.05f,10,10);
    glTranslatef(-0.1f, 0.0f, 0.0f);
    glutSolidSphere(0.05f,10,10);
    glPopMatrix();

// Draw Nose
    glColor3f(1.0f, 0.5f , 0.5f);
    glutSolidCone(0.08f,0.5f,10,2);
}

void renderScene(RepGameState* gameState) {

    // Clear Color and Depth Buffers

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Reset transformations
    glLoadIdentity();
    // Set the camera
    gluLookAt(  gameState->camera.x, 2.9f, gameState->camera.z,
            gameState->camera.x+gameState->camera.lx, 2.6f,  gameState->camera.z+gameState->camera.lz,
            0.0f, 1.0f,  0.0f);

        // Draw ground
    glColor3f(0.9f, 0.9f, 0.9f);
    glBegin(GL_QUADS);
        glVertex3f(-100.0f, 0.0f, -100.0f);
        glVertex3f(-100.0f, 0.0f,  100.0f);
        glVertex3f( 100.0f, 0.0f,  100.0f);
        glVertex3f( 100.0f, 0.0f, -100.0f);
    glEnd();

        // Draw 36 SnowMen
    for(int i = -3; i < 3; i++)
        for(int j=-3; j < 3; j++) {
            glPushMatrix();
            glTranslatef(i*10.0,0,j * 10.0);
            drawSnowMan();
            glPopMatrix();
        }

    glutSwapBuffers();
}

void display(RepGameState* gameState) {
    //glClearColor(gameState->colorR, gameState->colorG, gameState->colorB, 0);
    //drawSnowMan();
    renderScene(gameState);
    //glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    //glFlush();

}

void gameTick(RepGameState *gameState){
	if (gameState->exitGame){
		//Don't bother updating the state if the game is exiting
		return;
	}
    float fraction = 0.1f;
    if (gameState->input.arrows.left){
        gameState->camera.angle -= 0.01f;
        gameState->camera.lx = sin(gameState->camera.angle);
        gameState->camera.lz = -cos(gameState->camera.angle);
    }
    if (gameState->input.arrows.right){
        gameState->camera.angle += 0.01f;
        gameState->camera.lx = sin(gameState->camera.angle);
        gameState->camera.lz = -cos(gameState->camera.angle);
    }
    if (gameState->input.arrows.up){
        gameState->camera.x += gameState->camera.lx * fraction;
        gameState->camera.z += gameState->camera.lz * fraction;
    }
    if (gameState->input.arrows.down){
        gameState->camera.x -= gameState->camera.lx * fraction;
        gameState->camera.z -= gameState->camera.lz * fraction;
    }


    //TODO game logic
}

RepGameState globalGameState;

void arrowKeyDownInput(int key, int x, int y) {
	input_arrowKeyDownInput(&globalGameState, key, x, y);
}
void arrowKeyUpInput(int key, int x, int y) {
	input_arrowKeyUpInput(&globalGameState, key, x, y);
}
void mouseInput(int button, int state, int x, int y) {
	input_mouseInput(&globalGameState, button, state, x, y);
}
void keysInput(unsigned char key, int x, int y) {
    input_keysInput(&globalGameState, key, x, y);
}



void changeSize(int w, int h) {

    // Prevent a divide by zero, when window is too short
    // (you cant make a window of zero width).
    if(h == 0)
        h = 1;
    float ratio = 1.0* w / h;

    // Use the Projection Matrix
    glMatrixMode(GL_PROJECTION);

        // Reset Matrix
    glLoadIdentity();

    // Set the viewport to be the entire window
    glViewport(0, 0, w, h);

    // Set the correct perspective.
    gluPerspective(45,ratio,1,1000);

    // Get Back to the Modelview
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char**argv) {
    glutInit(&argc, argv);
    //glutInitWindowPosition(100,100);
    //glutInitWindowSize(500,500);
    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
    glEnable(GL_DEPTH_TEST);
    glutCreateWindow("RepGame");
    glutSpecialFunc(arrowKeyDownInput);
    glutSpecialUpFunc(arrowKeyUpInput);
    glutKeyboardFunc(keysInput);
    glutMouseFunc(mouseInput);
    glutReshapeFunc(changeSize);
    initilizeGameState(&globalGameState);
    while (!globalGameState.exitGame){
        glutMainLoopEvent();
        gameTick(&globalGameState);
        display(&globalGameState);
    }
    glutLeaveMainLoop();
    return 0;
}