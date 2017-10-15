#include "draw2d.h"
#include <GL/gl.h>
#include <GL/glut.h>

void draw2d_rect( double left, double top, double width, double height ) {
    glBegin( GL_QUADS );
    glVertex2f( left, top );
    glVertex2f( left + width, top );
    glVertex2f( left + width, top + height );
    glVertex2f( left, top + height );
    glEnd( );
}

void draw2d_border( double left, double top, double width, double height, double inlay ) {
    double doubleInlay = 2 * inlay;
    draw2d_rect( left, top, width, inlay );
    draw2d_rect( left, top + height - inlay, width, inlay );
    draw2d_rect( left, top + inlay, inlay, height - doubleInlay );
    draw2d_rect( left + width - inlay, top + inlay, inlay, height - doubleInlay );
}

void draw2d_cross( double center_h, double center_v, double thinkness, double length ) {
    draw2d_rect( center_h - length / 2, center_v - thinkness / 2, length, thinkness );
    draw2d_rect( center_h - thinkness / 2, center_v - length / 2, thinkness, length );
}

void draw2d_string( void *font, char *string ) {
    char *c;
    glPushMatrix( );
    for ( c = string; *c != '\0'; c++ ) {
        glutStrokeCharacter( font, *c );
    }
    glPopMatrix( );
}