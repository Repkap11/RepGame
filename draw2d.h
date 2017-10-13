#ifndef HEADER_DRAW_H
#define HEADER_DRAW_H

void draw2d_rect( double left, double top, double width, double height );
void draw2d_border( double left, double top, double width, double height, double inlay );
void draw2d_string( void *font, char *string );
void draw2d_cross( double center_h, double center_v, double thinkness, double length );

#endif