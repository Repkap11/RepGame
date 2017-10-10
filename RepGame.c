#include <GL/freeglut.h>
#include <GL/gl.h>
#include <math.h>
#include <time.h>
#include <unistd.h>


#include "RepGame.h"
#include "input.h"
#include "draw.h"

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


GLuint loadTexture( const char * filename )
{
    GLuint texture;
    int width, height;
    unsigned char * data;
    FILE * file;
    file = fopen( filename, "rb" );
    if ( file == NULL ){
        pr_debug("Error rendering texture %s", filename);
        return 0;
    }
    width = 512;
    height = 512;
    data = (unsigned char *)malloc( width * height * 3 );
    //int size = fseek(file,);
    fread( data, width * height * 3, 1, file );
    pr_debug("Finished reading file");
    fclose( file );

    glGenTextures( 1, &texture );
    glBindTexture( GL_TEXTURE_2D, texture );
    glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,GL_MODULATE );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST );

    float fLargest;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest);
    pr_debug("Largest Fitering:%f", fLargest);
    //glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);

    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_LINEAR );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,GL_REPEAT );
    glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,GL_REPEAT );
    gluBuild2DMipmaps( GL_TEXTURE_2D, 3, width, height,GL_RGB, GL_UNSIGNED_BYTE, data );
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);

    free(data);
    pr_debug("Done with loading texture");
    return texture;
}

GLuint texture;
void initilizeTexture(){
    texture = loadTexture("./bitmaps/colors.bmp");
}


void drawCube() {

    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, texture);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(0, 0, 0);
    glTexCoord2f(0, 1); glVertex3f(0, 1, 0);
    glTexCoord2f(1, 1); glVertex3f(1, 1, 0);
    glTexCoord2f(1, 0); glVertex3f(1, 0, 0);
    glEnd();

    glDisable(GL_TEXTURE_2D);

}

void renderScene(RepGameState* gameState) {

        // Draw ground
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        float groudSize = 1000.0f;
        glVertex3f(-1*groudSize, 0.0f, -1*groudSize);
        glVertex3f(-1*groudSize, 0.0f,  groudSize);
        glVertex3f( groudSize, 0.0f,  groudSize);
        glVertex3f( groudSize, 0.0f, -1*groudSize);
    glEnd();

    //glEnable(GL_LIGHTING);
    //glEnable(GL_LIGHT0);

    for(int i = -9; i < 9; i++)
        for(int j=-9; j < 9; j++) {
            glPushMatrix();
            glTranslatef(i*2.0,0,j * 2.0);
            drawCube();
            glPopMatrix();
        }
    //glDisable(GL_LIGHTING);
    //glDisable(GL_LIGHT0);

}

void pointCamera(RepGameState* gameState){
    glLoadIdentity();
    // Set the camera
    gluLookAt(  gameState->camera.x, 1.45f, gameState->camera.z,
            gameState->camera.x+gameState->camera.lx, 1.25f,  gameState->camera.z+gameState->camera.lz,
            0.0f, 1.0f,  0.0f);
}

char fps_str[50];
int frameCounter = 0;
void renderOverlay(RepGameState* gameState){

    int screenWidth = gameState->screen.width;
    int screenHeight = gameState->screen.height;
    glPushMatrix();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, screenWidth, screenHeight, 0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glColor4f(1.0f, 0.0f, 0.0, 0.5);
    draw_border(0,0,screenWidth,screenHeight, 10);

    if ((frameCounter % 8) == 0){
        frameCounter = 0;
        sprintf(fps_str, "FPS : %.2f", gameState->frameRate);
    }
    frameCounter += 1;
    glColor3f(0.0f, 0.0f, 0.0f);
    draw_bitmapString(19,19+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);
    draw_bitmapString(17,17+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);
    draw_bitmapString(19,17+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);
    draw_bitmapString(17,19+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);

    draw_bitmapString(18,19+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);
    draw_bitmapString(19,18+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);
    draw_bitmapString(17,18+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);
    draw_bitmapString(18,17+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0);

    glColor3f(1.0f, 1.0f, 1.0f);
    draw_bitmapString(18,18+18,GLUT_BITMAP_HELVETICA_18,fps_str, 0.1);

    // Making sure we can render 3d again
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

}

void display(RepGameState* gameState) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    renderScene(gameState);
    pointCamera(gameState);
    renderOverlay(gameState);
    glutSwapBuffers();
    glFlush();
}

void gameTick(RepGameState *gameState){
	if (gameState->input.exitGame){
		//Don't bother updating the state if the game is exiting
		return;
	}
    float fraction = 0.5f;
    float angle_diff = 0.05f;

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
    gluPerspective(45,ratio,1,1000);

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
    initilizeTexture();

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