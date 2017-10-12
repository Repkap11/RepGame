#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "ui_overlay.h"

char fps_str[ 50 ];
int frameCounter = 0;


void ui_overlay_drawBorder(RepGameState *gameState ){
    glColor4f( 0.0f, 0.0f, 0.0, 0.5 );
    draw_border( 0, 0, gameState->screen.width, gameState->screen.height, 10 );
}

void ui_overlay_drawFPS(RepGameState *gameState ){
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
}

void ui_overlay_drawCursor(RepGameState *gameState ){
    int screenWidth = gameState->screen.width;
    int screenHeight = gameState->screen.height;
    //int crossThinkness = 1;
    //int crossLength = 5;
    glColor4f( 0.0f, 0.0f, 0.0, 0.5);
    draw_cross(screenWidth/2, screenHeight/2, 3, 30);
}

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

    ui_overlay_drawBorder(gameState);
    ui_overlay_drawFPS(gameState);
    ui_overlay_drawCursor(gameState);

    // Making sure we can render 3d again
    glMatrixMode( GL_PROJECTION );
    glPopMatrix( );
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix( );
}
