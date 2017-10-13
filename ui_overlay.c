#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "ui_overlay.h"

char fps_str[ 50 ];
int frameCounter = 0;
#define BORDER_SIZE 10.0f

void ui_overlay_drawBorder( RepGameState *gameState ) {
    glColor4f( 0.0f, 0.0f, 0.0, 0.5 );
    draw_border( 0, 0, gameState->screen.width, gameState->screen.height, BORDER_SIZE );
}

void ui_overlay_drawFPS( RepGameState *gameState ) {
    sprintf( fps_str, "FPS: %4.1f", gameState->frameRate );

    glPushMatrix( );
    glTranslatef( 14, 30, 0 );
    glScalef( 0.15, 0.15, 0.15 );
    glRotatef( 180, 1, 0, 0 );
    glColor4f( 1.0f, 1.0f, 1.0, 1 );
    // glEnable( GL_LINE_SMOOTH );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glLineWidth( 2 );
    draw_string( GLUT_STROKE_MONO_ROMAN, fps_str );
    glPopMatrix( );
}

void ui_overlay_drawCursor( RepGameState *gameState ) {
    int screenWidth = gameState->screen.width;
    int screenHeight = gameState->screen.height;
    // int crossThinkness = 1;
    // int crossLength = 5;
    glColor4f( 0.0f, 0.0f, 0.0, 0.5 );
    draw_cross( screenWidth / 2, screenHeight / 2, 3, 30 );
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

    ui_overlay_drawBorder( gameState );
    ui_overlay_drawFPS( gameState );
    ui_overlay_drawCursor( gameState );

    // Making sure we can render 3d again
    glMatrixMode( GL_PROJECTION );
    glPopMatrix( );
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix( );
}
