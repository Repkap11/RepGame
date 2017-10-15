#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "ui_overlay.h"
#include <math.h>

int frameCounter = 0;
#define BORDER_SIZE 10.0f

void ui_overlay_drawBorder( RepGameState *gameState ) {
    glColor4f( 0.0f, 0.0f, 0.0, 0.5 );
    draw2d_border( 0, 0, gameState->screen.width, gameState->screen.height, BORDER_SIZE );
}

char fps_str[ 50 ];
char chunk_coords[ 50 ];
char block_coords[ 50 ];

void ui_overlay_drawText( RepGameState *gameState ) {
    sprintf( fps_str, "FPS: %4.1f", gameState->frameRate );
    sprintf( chunk_coords, "Block: x:%4.1f y:%4.1f z:%4.1f", gameState->camera.x, gameState->camera.y, gameState->camera.z );
    sprintf( block_coords, "Chunk: x:%4.0f y:%4.0f z:%4.0f", floor( gameState->camera.x / CHUNK_SIZE ), floor( gameState->camera.y / CHUNK_SIZE ), floor( gameState->camera.z / CHUNK_SIZE ) );

    glPushMatrix( );
    glTranslatef( 14, 30, 0 );
    glScalef( 0.15, 0.15, 0.15 );
    glRotatef( 180, 1, 0, 0 );
    glColor4f( 0.0f, 0.0f, 0.0, 1 );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glLineWidth( 2 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, fps_str );
    glTranslatef( 0, -180, 0 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, block_coords );
    glTranslatef( 0, -180, 0 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, chunk_coords );
    glPopMatrix( );
}

void ui_overlay_drawCursor( RepGameState *gameState ) {
    int screenWidth = gameState->screen.width;
    int screenHeight = gameState->screen.height;
    // int crossThinkness = 1;
    // int crossLength = 5;
    glColor4f( 0.0f, 0.0f, 0.0, 0.5 );
    draw2d_cross( screenWidth / 2, screenHeight / 2, 3, 30 );
}

void ui_overlay_draw( RepGameState *gameState ) {
    int screenWidth = gameState->screen.width;
    int screenHeight = gameState->screen.height;
    glPushMatrix( );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix( );
    glLoadIdentity( );
    glFrontFace( GL_CW );
    glOrtho( 0, screenWidth, screenHeight, 0, -1.0, 1.0 );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );

    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    ui_overlay_drawBorder( gameState );
    ui_overlay_drawText( gameState );
    ui_overlay_drawCursor( gameState );

    // Making sure we can render 3d again
    glMatrixMode( GL_PROJECTION );
    glFrontFace( GL_CCW );
    glPopMatrix( );
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix( );
}
