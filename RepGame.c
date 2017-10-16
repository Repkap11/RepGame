#include <GL/freeglut.h>
#include <GL/gl.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "RepGame.h"
#include "chunk.h"
#include "chunk_loader.h"
#include "draw3d.h"
#include "input.h"
#include "map_gen.h"
#include "textures.h"
#include "ui_overlay.h"

#define MOVEMENT_SENSITIVITY 10.0f // How sensitive the arrow keys are
#define CAMERA_SIZE 0.1f           // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 2.8f
#define PERSON_LOOKING -0.5f
#define DRAW_DISTANCE 10000
#define SKY_BOX_DISTANCE DRAW_DISTANCE * 0.9

int cubeDisplayList;

void initilizeGameState( RepGameState *gameState ) {
    gameState->input.exitGame = 0;
    gameState->camera.angle_H = 90;
    gameState->camera.angle_V = 0.0f;
    gameState->camera.x = 0.0f;
    gameState->camera.y = PERSON_HEIGHT;
    gameState->camera.z = 0.0f;
    chunk_loader_init( &gameState->gameChunks );
}
void cleanupGameState( RepGameState *gameState ) {
    chunk_loader_free_chunks( &gameState->gameChunks );
    // chunk_free( &gameState->gameChunk );
}

void drawScene( RepGameState *gameState ) {
    // draw3d_cube( );cleanupGameState
    chunk_loader_render_chunks( &gameState->gameChunks, gameState->camera.x, gameState->camera.y, gameState->camera.z );
    chunk_loader_draw_chunks( &gameState->gameChunks );

    glPushMatrix( );
    glTranslatef( gameState->camera.x, gameState->camera.y, gameState->camera.z );
    glScalef( SKY_BOX_DISTANCE, SKY_BOX_DISTANCE, SKY_BOX_DISTANCE );
    glRotatef( 270, 1, 0, 0 );
    draw3d_sphere( );
    glPopMatrix( );
}

void pointCamera( RepGameState *gameState ) {
    glRotatef( -gameState->camera.angle_V, -1, 0, 0 );
    glRotatef( -gameState->camera.angle_H, 0, -1, 0 );
    glTranslatef( -gameState->camera.x, -gameState->camera.y, -gameState->camera.z );
}

void display( RepGameState *gameState ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glLoadIdentity( );
    glColor3f( 1.0f, 1.0f, 1.0f );
    glLoadIdentity( );
    pointCamera( gameState );
    drawScene( gameState );
    ui_overlay_draw( gameState );
    glutSwapBuffers( );
    glFlush( );
}

void gameTick( RepGameState *gameState ) {
    if ( gameState->input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    float fraction = 0.010f * MOVEMENT_SENSITIVITY;
    // float angle_diff = 0.020f;

    // if ( gameState->input.mouse.buttons.middle ) {
    // update deltaAngle
    if ( gameState->input.mouse.currentPosition.x - gameState->input.mouse.previousPosition.x || gameState->input.mouse.currentPosition.y - gameState->input.mouse.previousPosition.y ) {
        // pr_debug( "Position Diff:%d %d", gameState->input.mouse.currentPosition.x - gameState->input.mouse.previousPosition.x, gameState->input.mouse.currentPosition.y - gameState->input.mouse.previousPosition.y );
    }

    gameState->camera.angle_H += ( gameState->input.mouse.currentPosition.x - gameState->input.mouse.previousPosition.x ) * 0.04f;
    gameState->camera.angle_V += ( gameState->input.mouse.currentPosition.y - gameState->input.mouse.previousPosition.y ) * 0.04f;

    // pr_debug( "Angle_V:%f", gameState->camera.angle_V );
    // Bpr_debug( "Angle_H:%f", gameState->camera.angle_H );

    float upAngleLimit = ( 90 ) - 0.001f;
    if ( gameState->camera.angle_V > upAngleLimit ) {
        gameState->camera.angle_V = upAngleLimit;
    }

    float downAngleLimit = -( 90 ) + 0.001f;
    if ( gameState->camera.angle_V < downAngleLimit ) {
        gameState->camera.angle_V = downAngleLimit;
    }
    //}

    if ( gameState->input.arrows.left ) {
        gameState->camera.x += gameState->camera.lz * fraction;
        gameState->camera.z -= gameState->camera.lx * fraction;
    }
    if ( gameState->input.arrows.right ) {
        gameState->camera.x -= gameState->camera.lz * fraction;
        gameState->camera.z += gameState->camera.lx * fraction;
    }
    if ( gameState->input.arrows.front ) {
        gameState->camera.x += gameState->camera.lx * fraction;
        gameState->camera.z += gameState->camera.lz * fraction;
    }
    if ( gameState->input.arrows.back ) {
        gameState->camera.x -= gameState->camera.lx * fraction;
        gameState->camera.z -= gameState->camera.lz * fraction;
    }
    if ( gameState->input.arrows.up ) {
        // pr_debug( "Up" );
        gameState->camera.y += fraction;
    }
    if ( gameState->input.arrows.down ) {
        // pr_debug( "Down" );
        gameState->camera.y -= fraction;
    }

    gameState->input.mouse.previousPosition.x = gameState->screen.width / 2;
    gameState->input.mouse.previousPosition.y = gameState->screen.height / 2;
    gameState->camera.lx = sin( gameState->camera.angle_H * M_PI / 180 );
    gameState->camera.ly = -tan( gameState->camera.angle_V * M_PI / 180 );
    gameState->camera.lz = -cos( gameState->camera.angle_H * M_PI / 180 );

    // pr_debug( "Looking x:%f z:%f", gameState->camera.lx, gameState->camera.lz );
}

RepGameState globalGameState;

void arrowKeyDownInput( int key, int x, int y ) {
    input_arrowKeyDownInput( &globalGameState.input, key, x, y );
}

void arrowKeyUpInput( int key, int x, int y ) {
    input_arrowKeyUpInput( &globalGameState.input, key, x, y );
}

void mouseInput( int button, int state, int x, int y ) {
    input_mouseInput( &globalGameState.input, button, state, x, y );
}

void keysInput( unsigned char key, int x, int y ) {
    input_keysInput( &globalGameState.input, key, x, y, 1 );
}

void keysInputUp( unsigned char key, int x, int y ) {
    input_keysInput( &globalGameState.input, key, x, y, 0 );
}

void mouseMove( int x, int y ) {
    input_mouseMove( &globalGameState.input, x, y );
}

void changeSize( int w, int h ) {

    // pr_debug( "Screen Size Change:%dx%d", w, h );
    globalGameState.screen.width = w;
    globalGameState.screen.height = h;

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;

    // Prevent a divide by zero, when window is too short
    // (you cant make a window of zero width).
    if ( h == 0 )
        h = 1;
    float ratio = 1.0 * w / h;

    // Use the Projection Matrix
    glMatrixMode( GL_PROJECTION );

    // Reset Matrix
    glLoadIdentity( );

    // Set the viewport to be the entire window
    glViewport( 0, 0, w, h );

    // Set the correct perspective.
    gluPerspective( 60, ratio, CAMERA_SIZE, DRAW_DISTANCE );

    // Get Back to the Modelview
    glMatrixMode( GL_MODELVIEW );
}

double fps_ms = ( 1.0 / 60.0 ) * 1000.0;

int main( int argc, char **argv ) {
    glutInit( &argc, argv );
    // glutInitContextVersion( 3, 1 );
    // glutInitContextFlags( GLUT_DEBUG );
    glutInitDisplayMode( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA );

    glutCreateWindow( "RepGame" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glEnable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glEnable( GL_MULTISAMPLE );

    glutSpecialFunc( arrowKeyDownInput );
    glutSpecialUpFunc( arrowKeyUpInput );

    glutSetCursor( GLUT_CURSOR_NONE );

    glutKeyboardFunc( keysInput );
    glutKeyboardUpFunc( keysInputUp );
    glutMouseFunc( mouseInput );
    glutReshapeFunc( changeSize );
    glutPassiveMotionFunc( mouseMove );
    glutMotionFunc( mouseMove );
    textures_populate( );
    initilizeGameState( &globalGameState );

    struct timespec tstart = {0, 0}, tend = {0, 0};
    clock_gettime( CLOCK_MONOTONIC, &tstart );
    tend = tstart;
    while ( !globalGameState.input.exitGame ) {
        glutMainLoopEvent( );
        // input_set_enable_mouse( 0 );
        glutWarpPointer( globalGameState.screen.width / 2, globalGameState.screen.height / 2 );
        // glutMainLoopEvent( );
        // input_set_enable_mouse( 1 );
        gameTick( &globalGameState );
        display( &globalGameState );

        clock_gettime( CLOCK_MONOTONIC, &tstart );

        double diff_ms = ( ( ( double )tstart.tv_sec + 1.0e-9 * tstart.tv_nsec ) - ( ( double )tend.tv_sec + 1.0e-9 * tend.tv_nsec ) ) * 1000.0;
        tend = tstart;
        // pr_debug("Time Diff ms:%f", diff_ms);
        globalGameState.frameRate = 1.0 / ( diff_ms / 1000.0 );
        double wait_time_ms = fps_ms - diff_ms;
        if ( wait_time_ms > 1.0 ) {
            int wait_time_us = ( int )( wait_time_ms * 1000.0 );
            // pr_debug("WaitTime_us:%d", wait_time_us);
            usleep( wait_time_us );
            ( void )wait_time_us;
        }
    }
    cleanupGameState( &globalGameState );
    glutLeaveMainLoop( );
    return 0;
}