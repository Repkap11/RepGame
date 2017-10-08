#include <GL/glut.h>
#include <GL/gl.h>
#include <stdio.h>


#define DEBUG 1

#define pr_debug(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s():"fmt"\n", __FILE__, \
                                __LINE__, __func__, ##__VA_ARGS__); } while (0)

void display() {
    glClearColor(1,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);
    pr_debug("TEST");
    glFlush();

}

int main(int argc, char**argv) {
    glutInit(&argc, argv);
    //glutInitWindowPosition(100,100);
    //glutInitWindowSize(500,500);
    glutCreateWindow("RepGame");
    glutDisplayFunc(display);
    glutMainLoop();
    return 0;
}