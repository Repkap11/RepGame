#ifndef HEADER_DRAW_H
#define HEADER_DRAW_H

void draw_rect( double left, double top, double width, double height );
void draw_border( double left, double top, double width, double height, double inlay );
void draw_bitmapString( float left, float top, void *font, char *string, float depth );
void draw_cross( double center_h, double center_v, double thinkness, double length );

#endif