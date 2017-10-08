#include<GL/glut.h>
#include<GL/gl.h>

void display() {
    glClearColor(1,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
}

int main(int argc, char**argv) {
    glutInit(&argc, argv);
    //glutInitWindowPosition(100,100);
    //glutInitWindowSize(500,500);
    glutCreateWindow("Hello World");
    glutDisplayFunc(display);
    glutMainLoop();
    return 0;
}