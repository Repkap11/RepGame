#include <GL/freeglut.h>
#include <GL/gl.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "RepGame.h"
#include "block_definitions.h"
#include "chunk.h"
#include "chunk_loader.h"
#include "draw3d.h"
#include "input.h"
#include "map_gen.h"
#include "terrain_loading_thread.h"
#include "textures.h"
#include "ui_overlay.h"

#define SKY_BOX_DISTANCE DRAW_DISTANCE * 0.9

RepGameState globalGameState;

static inline void initilizeGameState( ) {
    globalGameState.input.exitGame = 0;
    globalGameState.input.limit_fps = 1;
    globalGameState.camera.angle_H = STARTING_ANGLE_H;
    globalGameState.camera.angle_V = STARTING_ANGLE_V;
    globalGameState.camera.x = 0.0f;
    globalGameState.camera.y = PERSON_HEIGHT;
    globalGameState.camera.z = 0.0f;
    textures_populate( );
    block_definitions_initilize_definitions( );
    chunk_loader_init( &globalGameState.gameChunks );
    // pr_debug( "RepGame init done" );
}
static inline void cleanupGameState( ) {
    chunk_loader_free_chunks( &globalGameState.gameChunks );
    block_definitions_free_definitions( );
    textures_free( );
    // pr_debug( "RepGame cleanup done" );
}

static inline void drawScene( ) {
    // draw3d_cube( );cleanupGameState
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    chunk_loader_render_chunks( &globalGameState.gameChunks, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );
    chunk_loader_draw_chunks( &globalGameState.gameChunks );

    glPushMatrix( );
    glTranslatef( globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );
    glScalef( SKY_BOX_DISTANCE, SKY_BOX_DISTANCE, SKY_BOX_DISTANCE );
    glRotatef( 270, 1, 0, 0 );
    draw3d_sphere( );
    glPopMatrix( );
}

static inline void pointCamera( ) {
    glRotatef( -globalGameState.camera.angle_V, -1, 0, 0 );
    glRotatef( -globalGameState.camera.angle_H, 0, -1, 0 );
    glTranslatef( -globalGameState.camera.x, -globalGameState.camera.y, -globalGameState.camera.z );
}

static inline void display( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glLoadIdentity( );
    pointCamera( );
    drawScene( );
    ui_overlay_draw( &globalGameState );
    glutSwapBuffers( );
}

static inline void gameTick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    float fraction = 0.010f * MOVEMENT_SENSITIVITY;
    // float angle_diff = 0.020f;

    // if ( input.mouse.buttons.middle ) {
    // update deltaAngle
    if ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x || globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) {
        // pr_debug( "Position Diff:%d %d", input.mouse.currentPosition.x - input.mouse.previousPosition.x, input.mouse.currentPosition.y - input.mouse.previousPosition.y );
    }

    globalGameState.camera.angle_H += ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x ) * 0.04f;
    globalGameState.camera.angle_V += ( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) * 0.04f;

    // pr_debug( "Angle_V:%f", globalGameState.camera.angle_V );
    // Bpr_debug( "Angle_H:%f", globalGameState.camera.angle_H );

    float upAngleLimit = 90 - 0.001f;
    if ( globalGameState.camera.angle_V > upAngleLimit ) {
        globalGameState.camera.angle_V = upAngleLimit;
    }

    float downAngleLimit = -90 + 0.001f;
    if ( globalGameState.camera.angle_V < downAngleLimit ) {
        globalGameState.camera.angle_V = downAngleLimit;
    }
    //}

    if ( globalGameState.input.arrows.left ) {
        globalGameState.camera.x += globalGameState.camera.lz * fraction;
        globalGameState.camera.z -= globalGameState.camera.lx * fraction;
    }
    if ( globalGameState.input.arrows.right ) {
        globalGameState.camera.x -= globalGameState.camera.lz * fraction;
        globalGameState.camera.z += globalGameState.camera.lx * fraction;
    }
    if ( globalGameState.input.arrows.front ) {
        globalGameState.camera.x += globalGameState.camera.lx * fraction;
        globalGameState.camera.z += globalGameState.camera.lz * fraction;
    }
    if ( globalGameState.input.arrows.back ) {
        globalGameState.camera.x -= globalGameState.camera.lx * fraction;
        globalGameState.camera.z -= globalGameState.camera.lz * fraction;
    }
    if ( globalGameState.input.arrows.up ) {
        // pr_debug( "Up" );
        globalGameState.camera.y += fraction;
    }
    if ( globalGameState.input.arrows.down ) {
        // pr_debug( "Down" );
        globalGameState.camera.y -= fraction;
    }

    globalGameState.input.mouse.previousPosition.x = globalGameState.screen.width / 2;
    globalGameState.input.mouse.previousPosition.y = globalGameState.screen.height / 2;
    globalGameState.camera.lx = sin( globalGameState.camera.angle_H * ( M_PI / 180 ) );
    globalGameState.camera.ly = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    globalGameState.camera.lz = -cos( globalGameState.camera.angle_H * ( M_PI / 180 ) );
    // pr_debug( "Looking x:%f z:%f", globalGameState.camera.lx, globalGameState.camera.lz );
}

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
    gluPerspective( CAMERA_FOV, ratio, CAMERA_SIZE, DRAW_DISTANCE );

    // Get Back to the Modelview
    glMatrixMode( GL_MODELVIEW );
}

double fps_ms = ( 1.0 / FPS_LIMIT ) * 1000.0;

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
    initilizeGameState( );
    int status = terrain_loading_thread_start( );
    if ( status ) {
        pr_debug( "Terrain loading thread failed to start." );
    }

    struct timespec tstart = {0, 0}, tend = {0, 0};
    clock_gettime( CLOCK_MONOTONIC, &tstart );
    tend = tstart;
    while ( !globalGameState.input.exitGame ) {
        glutMainLoopEvent( );
        // input_set_enable_mouse( 0 );
        glutWarpPointer( globalGameState.screen.width / 2, globalGameState.screen.height / 2 );
        // glutMainLoopEvent( );
        // input_set_enable_mouse( 1 );
        gameTick( );
        display( );

        clock_gettime( CLOCK_MONOTONIC, &tstart );

        double diff_ms = ( ( ( double )tstart.tv_sec + 1.0e-9 * tstart.tv_nsec ) - ( ( double )tend.tv_sec + 1.0e-9 * tend.tv_nsec ) ) * 1000.0;
        tend = tstart;
        // pr_debug("Time Diff ms:%f", diff_ms);
        globalGameState.frame_rate = 1.0 / ( diff_ms / 1000.0 );
        if ( globalGameState.input.limit_fps ) {
            double wait_time_ms = fps_ms - diff_ms;
            if ( wait_time_ms > 1.0 ) {
                int wait_time_us = ( int )( wait_time_ms * 1000.0 );
                // pr_debug("WaitTime_us:%d", wait_time_us);
                usleep( wait_time_us );
            }
        }
    }
    terrain_loading_thread_stop( );
    cleanupGameState( );
    glutLeaveMainLoop( );
    return 0;
}