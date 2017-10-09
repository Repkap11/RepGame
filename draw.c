#include <GL/gl.h>

void draw_rect(double left, double top, double width, double height){
    glBegin(GL_QUADS);
        glVertex2f(left, top);
        glVertex2f(left + width, top);
        glVertex2f(left + width, top + height);
        glVertex2f(left, top + height);
    glEnd();
}

void draw_border(double left, double top, double width, double height, double inlay){
    double doubleInlay = 2*inlay;
    draw_rect(left,top,width, inlay);
    draw_rect(left,top+height-inlay,width, inlay);
    draw_rect(left,top+inlay,inlay, height - doubleInlay);
    draw_rect(left+width-inlay,top+inlay, inlay, height - doubleInlay);
}