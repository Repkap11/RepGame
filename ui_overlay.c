#include <GL/gl.h>
#include <GL/glut.h>

#include "draw.h"
#include "ui_overlay.h"

char fps_str[ 50 ];
int frameCounter = 0;

void ui_overlay_draw( RepGameState *gameState ) {
    int screenWidth = gameState->screen.width;
    int screenHeight = gameState->screen.height;
    glPushMatrix( );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix( );
    glLoadIdentity( );
    glOrtho( 0, screenWidth, screenHeight, 0, -1.0, 1.0 );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );

    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_BLEND );
    glColor4f( 1.0f, 0.0f, 0.0, 0.5 );
    draw_border( 0, 0, gameState->screen.width, gameState->screen.height, 10 );

    if ( ( frameCounter % 8 ) == 0 ) {
        frameCounter = 0;
        sprintf( fps_str, "FPS : %.2f", gameState->frameRate );
    }
    frameCounter += 1;
    glColor3f( 0.0f, 0.0f, 0.0f );
    draw_bitmapString( 19, 19 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );
    draw_bitmapString( 17, 17 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );
    draw_bitmapString( 19, 17 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );
    draw_bitmapString( 17, 19 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );

    draw_bitmapString( 18, 19 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );
    draw_bitmapString( 19, 18 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );
    draw_bitmapString( 17, 18 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );
    draw_bitmapString( 18, 17 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0 );

    glColor3f( 1.0f, 1.0f, 1.0f );
    draw_bitmapString( 18, 18 + 18, GLUT_BITMAP_HELVETICA_18, fps_str, 0.1 );

    // Making sure we can render 3d again
    glMatrixMode( GL_PROJECTION );
    glPopMatrix( );
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix( );
}
