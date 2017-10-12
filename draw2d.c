#include "draw2d.h"
#include <GL/gl.h>
#include <GL/glut.h>

void draw_rect( double left, double top, double width, double height ) {
    glBegin( GL_QUADS );
    glVertex2f( left, top );
    glVertex2f( left + width, top );
    glVertex2f( left + width, top + height );
    glVertex2f( left, top + height );
    glEnd( );
}

void draw_border( double left, double top, double width, double height, double inlay ) {
    double doubleInlay = 2 * inlay;
    draw_rect( left, top, width, inlay );
    draw_rect( left, top + height - inlay, width, inlay );
    draw_rect( left, top + inlay, inlay, height - doubleInlay );
    draw_rect( left + width - inlay, top + inlay, inlay, height - doubleInlay );
}

void draw_cross( double center_h, double center_v, double thinkness, double length ) {
    draw_rect( center_h - length / 2, center_v - thinkness / 2, length, thinkness );
    draw_rect( center_h - thinkness / 2, center_v - length / 2, thinkness, length );
    // draw_rect( left, top + height - inlay, width, inlay );
    // draw_rect( left, top + inlay, inlay, height - doubleInlay );
    // draw_rect( left + width - inlay, top + inlay, inlay, height - doubleInlay );
}

void draw_bitmapString( float left, float top, void *font, char *string, float depth ) {
    char *c;
    glRasterPos3f( left, top, depth );
    for ( c = string; *c != '\0'; c++ ) {
        glutBitmapCharacter( font, *c );
    }
}