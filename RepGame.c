#include <GL/freeglut.h>
#include <GL/gl.h>
#include <math.h>
#include <time.h>
#include <unistd.h>


#include "RepGame.h"
#include "input.h"
#include "draw.h"
#include "ui_overlay.h"
#include "textures.h"

#define ENABLE_LIGHT 0

void initilizeGameState(RepGameState* gameState){
    gameState->input.exitGame = 0;
    gameState->colorR = 1;
    gameState->colorG = 0;
    gameState->colorB = 0;
    gameState->camera.angle = 0.0f;
    gameState->camera.lx = 0.0f;
    gameState->camera.lz = -1.0f;
    gameState->camera.x = 0.0f;
    gameState->camera.z = 5.0f;
}

void drawCube() {

    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, textures_getTestTexture());

    // White side - BACK
    glBegin(GL_POLYGON);
    //glColor3f(   1.0,  1.0, 1.0 );
    glTexCoord2f(0, 0); glVertex3f(  1.0, 0.0, 1.0 );
    glTexCoord2f(0, 2); glVertex3f(  1.0,  1.0, 1.0 );
    glTexCoord2f(2, 2); glVertex3f( 0.0,  1.0, 1.0 );
    glTexCoord2f(2, 0); glVertex3f( 0.0, 0.0, 1.0 );
    glEnd();

    // Purple side - RIGHT
    glBegin(GL_POLYGON);
    //glColor3f(  1.0,  0.0,  1.0 );
    glTexCoord2f(0, 0); glVertex3f( 1.0, 0.0, 0.0 );
    glTexCoord2f(0, 1); glVertex3f( 1.0,  1.0, 0.0 );
    glTexCoord2f(1, 1); glVertex3f( 1.0,  1.0,  1.0 );
    glTexCoord2f(1, 0); glVertex3f( 1.0, 0.0,  1.0 );
    glEnd();

    // Green side - LEFT
    glBegin(GL_POLYGON);
    //glColor3f(   0.0,  1.0,  0.0 );
    glTexCoord2f(0, 0); glVertex3f( 0.0, 0.0,  1.0 );
    glTexCoord2f(0, 1); glVertex3f( 0.0,  1.0,  1.0 );
    glTexCoord2f(1, 1); glVertex3f( 0.0,  1.0, 0.0 );
    glTexCoord2f(1, 0); glVertex3f( 0.0, 0.0, 0.0 );
    glEnd();

    // Blue side - TOP
    glBegin(GL_POLYGON);
    //glColor3f(   0.0,  0.0,  1.0 );
    glTexCoord2f(0, 0); glVertex3f(  1.0,  1.0,  1.0 );
    glTexCoord2f(0, 1); glVertex3f(  1.0,  1.0, 0.0 );
    glTexCoord2f(1, 1); glVertex3f( 0.0,  1.0, 0.0 );
    glTexCoord2f(1, 0); glVertex3f( 0.0,  1.0,  1.0 );
    glEnd();

    // Red side - BOTTOM
    glBegin(GL_POLYGON);
    //glColor3f(   1.0,  0.0,  0.0 );
    glTexCoord2f(0, 0); glVertex3f(  1.0, 0.0, 0.0 );
    glTexCoord2f(0, 1); glVertex3f(  1.0, 0.0,  1.0 );
    glTexCoord2f(1, 1); glVertex3f( 0.0, 0.0,  1.0 );
    glTexCoord2f(1, 0); glVertex3f( 0.0, 0.0, 0.0 );
    glEnd();


    // //Side
    // glTexCoord2f(0, 0); glVertex3f(0, 0, 0);
    // glTexCoord2f(0, 1); glVertex3f(0, 1, 0);
    // glTexCoord2f(1, 1); glVertex3f(1, 1, 0);
    // glTexCoord2f(1, 0); glVertex3f(1, 0, 0);

    // glTexCoord2f(0, 0); glVertex3f(0, 1, 0);
    // glTexCoord2f(0, 1); glVertex3f(0, 1, 1);
    // glTexCoord2f(1, 1); glVertex3f(0, 0, 1);
    // glTexCoord2f(1, 0); glVertex3f(0, 0, 0);

    // glTexCoord2f(0, 0); glVertex3f(1, 0, 0);
    // glTexCoord2f(0, 1); glVertex3f(1, 0, 1);
    // glTexCoord2f(1, 1); glVertex3f(0, 0, 1);
    // glTexCoord2f(1, 0); glVertex3f(0, 0, 0);

    glDisable(GL_TEXTURE_2D);

}

void renderScene(RepGameState* gameState) {

        // Draw ground
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        float groudSize = 1000.0f;
        glVertex3f(-1*groudSize, -0.001f, -1*groudSize);
        glVertex3f(-1*groudSize, -0.001f,  groudSize);
        glVertex3f( groudSize, -0.001f,  groudSize);
        glVertex3f( groudSize, -0.001f, -1*groudSize);
    glEnd();

    if (ENABLE_LIGHT){
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
    }

    for(int i = 0; i < 2; i++) {
        for(int j=0; j < 2; j++) {
            glPushMatrix();
            glTranslatef(i*2.0,0,j * 2.0);
            drawCube();
            glPopMatrix();
        }
    }
    if (ENABLE_LIGHT){
        glDisable(GL_LIGHTING);
        glDisable(GL_LIGHT0);
    }

}

void pointCamera(RepGameState* gameState){
    glLoadIdentity();
    // Set the camera
    gluLookAt(  gameState->camera.x, 1.30f, gameState->camera.z,
            gameState->camera.x+gameState->camera.lx, 1.0f,  gameState->camera.z+gameState->camera.lz,
            0.0f, 1.0f,  0.0f);
}



void display(RepGameState* gameState) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderScene(gameState);
    pointCamera(gameState);
    ui_overlay_draw(gameState);
    glutSwapBuffers();
    glFlush();
}

void gameTick(RepGameState *gameState){
	if (gameState->input.exitGame){
		//Don't bother updating the state if the game is exiting
		return;
	}
    float fraction = 0.08f;
    float angle_diff = 0.020f;

    if (gameState->input.mouse.buttons.middle){
        // update deltaAngle
        pr_debug("Position Diff:%d",gameState->input.mouse.currentPosition.x - gameState->input.mouse.previousPosition.x);
        gameState->camera.angle += (gameState->input.mouse.currentPosition.x - gameState->input.mouse.previousPosition.x) * 0.002f;
    }

    if (gameState->input.arrows.left){
        gameState->camera.angle -= angle_diff;
    }
    if (gameState->input.arrows.right){
        gameState->camera.angle +=angle_diff;
    }

    if (gameState->input.arrows.up){
        gameState->camera.x += gameState->camera.lx * fraction;
        gameState->camera.z += gameState->camera.lz * fraction;
    }
    if (gameState->input.arrows.down){
        gameState->camera.x -= gameState->camera.lx * fraction;
        gameState->camera.z -= gameState->camera.lz * fraction;
    }

    gameState->input.mouse.previousPosition.x = gameState->input.mouse.currentPosition.x;
    gameState->camera.lx = sin(gameState->camera.angle);
    gameState->camera.lz = -cos(gameState->camera.angle);
}

RepGameState globalGameState;

void arrowKeyDownInput(int key, int x, int y) {
	input_arrowKeyDownInput(&globalGameState.input, key, x, y);
}
void arrowKeyUpInput(int key, int x, int y) {
	input_arrowKeyUpInput(&globalGameState.input, key, x, y);
}
void mouseInput(int button, int state, int x, int y) {
	input_mouseInput(&globalGameState.input, button, state, x, y);
}
void keysInput(unsigned char key, int x, int y) {
    input_keysInput(&globalGameState.input, key, x, y);
}
void mouseMove(int x, int y){
    input_mouseMove(&globalGameState.input, x, y);
}



void changeSize(int w, int h) {

    pr_debug("Screen Size Change:%dx%d", w,h);
    globalGameState.screen.width = w;
    globalGameState.screen.height = h;

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
    gluPerspective(45,ratio,0.1f,1000);

    // Get Back to the Modelview
    glMatrixMode(GL_MODELVIEW);
}

double fps_ms = (1.0/60.0)*1000.0;

int main(int argc, char**argv) {
    glutInit(&argc, argv);

    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);

    glutCreateWindow("RepGame");
    glEnable(GL_DEPTH_TEST);
    glutSpecialFunc(arrowKeyDownInput);
    glutSpecialUpFunc(arrowKeyUpInput);

    glutKeyboardFunc(keysInput);
    glutMouseFunc(mouseInput);
    glutReshapeFunc(changeSize);
    glutMotionFunc(mouseMove);
    initilizeGameState(&globalGameState);
    textures_populate();

    struct timespec tstart={0,0}, tend={0,0};
    clock_gettime(CLOCK_MONOTONIC, &tstart);
    tend = tstart;
    while (!globalGameState.input.exitGame){
        glutMainLoopEvent();
        gameTick(&globalGameState);
        display(&globalGameState);

        clock_gettime(CLOCK_MONOTONIC, &tstart);

        double diff_ms = (((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec) - ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec)) * 1000.0;
        tend = tstart;
        //pr_debug("Time Diff ms:%f", diff_ms);
        globalGameState.frameRate = 1.0/(diff_ms / 1000.0);
        double wait_time_ms = fps_ms - diff_ms;
        if (wait_time_ms > 1.0){
            int wait_time_us = (int)(wait_time_ms * 1000.0);
            //pr_debug("WaitTime_us:%d", wait_time_us);
            usleep(wait_time_us);
            (void) wait_time_us;
        }

    }
    glutLeaveMainLoop();
    return 0;
}