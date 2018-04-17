#include <GL/gl.h>
#include <GL/glut.h>

#include "draw2d.h"
#include "ui_overlay.h"
#include <math.h>

int frameCounter = 0;
#define BORDER_SIZE 0.0f

void ui_overlay_drawBorder( RepGameState *gameState ) {
    glColor4f( 0.0f, 0.0f, 0.0, 0.5 );
    draw2d_border( 0, 0, gameState->screen.width, gameState->screen.height, BORDER_SIZE );
}

char fps_str[ 50 ];
char chunk_coords[ 50 ];
char block_coords[ 50 ];
char limit_fps[ 50 ];
char diff_coords[ 50 ];

void ui_overlay_drawText( RepGameState *gameState ) {
    sprintf( fps_str, "FPS: %4.1f", gameState->frame_rate );
    sprintf( block_coords, "Coord: x:%7.3f y:%7.3f z:%7.3f", gameState->camera.x, gameState->camera.y, gameState->camera.z );
    sprintf( chunk_coords, "Chunk: x:%5.0f y:%5.0f z:%5.0f", floor( gameState->camera.x / CHUNK_SIZE ), floor( gameState->camera.y / CHUNK_SIZE ), floor( gameState->camera.z / CHUNK_SIZE ) );
    sprintf( diff_coords, "Block: x:%5.2f y:%5.2f z:%5.2f",      //
             gameState->camera.x - floor( gameState->camera.x ), //
             gameState->camera.y - floor( gameState->camera.y ), //
             gameState->camera.z - floor( gameState->camera.z ) );
    sprintf( limit_fps, "Limit FPS: %s", gameState->input.limit_fps ? "true" : "false" );

    glPushMatrix( );
    // glTranslatef( 15, 15, 0 );
    glTranslatef( 2, 17, 0 );
    glScalef( 0.12, 0.12, 0.12 );
    glRotatef( 180, 1, 0, 0 );
    glColor4f( 0.0f, 0.0f, 0.0, 1 );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glLineWidth( 3 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, fps_str );
    glTranslatef( 0, -180, 0 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, block_coords );
    glTranslatef( 0, -180, 0 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, chunk_coords );
    glTranslatef( 0, -180, 0 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, diff_coords );
    glTranslatef( 0, -180, 0 );
    draw2d_string( GLUT_STROKE_MONO_ROMAN, limit_fps );
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

    // ui_overlay_drawBorder( gameState );
    ui_overlay_drawText( gameState );
    ui_overlay_drawCursor( gameState );

    // Making sure we can render 3d again
    glMatrixMode( GL_PROJECTION );
    glFrontFace( GL_CCW );
    glPopMatrix( );
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix( );
}
