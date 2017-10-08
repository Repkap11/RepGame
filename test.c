#include <GL/glut.h>
#include <GL/gl.h>
#include <stdio.h>


#define DEBUG 1

#define pr_debug(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s():"fmt"\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

void display() {
    glClearColor(1,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);
    pr_debug("TEST");
    glFlush();

}

void arrowKeyInput(int key, int x, int y) {
	switch(key) {
		case GLUT_KEY_UP:
			pr_debug("Up Arrow");
		break;
		case GLUT_KEY_DOWN:
			pr_debug("Down Arrow");
		break;
		case GLUT_KEY_LEFT:
			pr_debug("Left Arrow");

		break;
		case GLUT_KEY_RIGHT:
			pr_debug("Right Arrow");
		break;
	}
}


void mouseInput(int button, int state, int x, int y){

	switch(button) {
		case GLUT_LEFT_BUTTON:
			pr_debug("Left Click");
		break;
		case GLUT_RIGHT_BUTTON:
			pr_debug("Right Click");
		break;
		case GLUT_MIDDLE_BUTTON:
			pr_debug("Middle Click");
	}
}

int main(int argc, char**argv) {
    glutInit(&argc, argv);
    //glutInitWindowPosition(100,100);
    //glutInitWindowSize(500,500);
    glutCreateWindow("RepGame");
    glutDisplayFunc(display);
    glutSpecialFunc(arrowKeyInput);
    glutMouseFunc(mouseInput);
    glutMainLoop();
    return 0;
}