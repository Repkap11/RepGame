#include <GL/glew.h>
#include <GL/freeglut.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "RepGame.h"
#include "draw3d.h"
#include "input.h"
#include "textures.h"
#include "ui_overlay.h"
#include "shaders.h"

#define MOVEMENT_SENSITIVITY 10.0f // How sensitive the arrow keys are
#define CAMERA_SIZE 0.1f           // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 5.0f
#define PERSON_LOOKING -0.5f

int cubeDisplayList;
void initilizeGameState( RepGameState *gameState ) {
    gameState->input.exitGame = 0;
    gameState->camera.angle_H = 0.0f;
    gameState->camera.angle_V = 0.3f;
    gameState->camera.lz = -1.0f;
    gameState->camera.x = 0.5f;
    gameState->camera.y = PERSON_HEIGHT;
    gameState->camera.z = 5.0f;
}

uint g_program;
static GLuint g_programCameraPositionLocation;
static GLuint g_programLightPositionLocation;
static GLuint g_programCameraUnitLocation;

#define NUM_LIGHTS 1
static float g_lightPosition[ 3 ];
static float g_cameraPosition[ 3 ];
static float g_cameraUnit[ 3 ];

void renderShaders( RepGameState *gameState ) {
    g_programCameraPositionLocation = glGetUniformLocation( g_program, "cameraPosition" );
    g_programCameraUnitLocation = glGetUniformLocation( g_program, "cameraUnit" );

    g_programLightPositionLocation = glGetUniformLocation( g_program, "lightPosition" );

    g_cameraPosition[ 0 ] = gameState->camera.x;
    g_cameraPosition[ 1 ] = gameState->camera.y;
    g_cameraPosition[ 2 ] = gameState->camera.z;

    g_cameraUnit[ 0 ] = gameState->camera.lx;
    g_cameraUnit[ 1 ] = gameState->camera.ly;
    g_cameraUnit[ 2 ] = gameState->camera.lz;

    g_lightPosition[ 0 ] = 0;
    g_lightPosition[ 1 ] = 0;
    g_lightPosition[ 2 ] = 0;

    glUniform3fv( g_programCameraPositionLocation, 1, g_cameraPosition );
    glUniform3fv( g_programCameraUnitLocation, 1, g_cameraUnit );
    glUniform3fv( g_programLightPositionLocation, 1, g_lightPosition );
}

void renderScene( RepGameState *gameState ) {
    draw3d_cube( );
    glUseProgram( g_program );
    renderShaders( gameState );

    glCallList( cubeDisplayList );
    glUseProgram( 0 );

    glPushMatrix( );
    glTranslatef( gameState->camera.x, 0, gameState->camera.z );
    glScalef( 100, 100, 100 );
    glRotatef( 270, 1, 0, 0 );
    draw3d_sphere( );
    glPopMatrix( );
}

void createDisplayList( ) {
    glPushMatrix( );
    cubeDisplayList = glGenLists( 1 );
    // compile the display list, store a triangle in it
    glNewList( cubeDisplayList, GL_COMPILE );

    for ( int i = -50; i < 50; i++ ) {
        for ( int j = 0; j < 1; j++ ) {
            for ( int k = -50; k < 50; k++ ) {
                glPushMatrix( );
                glTranslatef( i, j, k );
                draw3d_cube( );
                glPopMatrix( );
            }
        }
    }

    for ( int i = -8; i < 8; i++ ) {
        for ( int j = -8; j < 8; j++ ) {
            for ( int k = -8; k < 8; k++ ) {
                if ( fabs( i + k ) > j ) {
                    glPushMatrix( );
                    glTranslatef( i, j, k );
                    draw3d_cube( );
                    glPopMatrix( );
                }
            }
        }
    }

    // draw3d_cube( );
    glEndList( );
    glPopMatrix( );
}

void pointCamera( RepGameState *gameState ) {
    glLoadIdentity( );
    // Set the camera
    gluLookAt( gameState->camera.x, gameState->camera.y, gameState->camera.z,                                                                      // Eye point
               gameState->camera.x + gameState->camera.lx, gameState->camera.y + gameState->camera.ly, gameState->camera.z + gameState->camera.lz, // look at
               0.0f, 1.0f, 0.0f );
}

void display( RepGameState *gameState ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glColor3f( 1.0f, 1.0f, 1.0f );

    renderScene( gameState );
    pointCamera( gameState );
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

    gameState->camera.angle_H += ( gameState->input.mouse.currentPosition.x - gameState->input.mouse.previousPosition.x ) * 0.002f;
    gameState->camera.angle_V += ( gameState->input.mouse.currentPosition.y - gameState->input.mouse.previousPosition.y ) * 0.002f;

    // pr_debug( "Angle_V:%f", gameState->camera.angle_V );

    float upAngleLimit = ( M_PI / 2 ) - 0.001f;
    if ( gameState->camera.angle_V > upAngleLimit ) {
        gameState->camera.angle_V = upAngleLimit;
    }

    float downAngleLimit = -( M_PI / 2 ) + 0.001f;
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
    if ( gameState->input.arrows.up ) {
        gameState->camera.x += gameState->camera.lx * fraction;
        gameState->camera.z += gameState->camera.lz * fraction;
    }
    if ( gameState->input.arrows.down ) {
        gameState->camera.x -= gameState->camera.lx * fraction;
        gameState->camera.z -= gameState->camera.lz * fraction;
    }

    gameState->input.mouse.previousPosition.x = gameState->screen.width / 2;
    gameState->input.mouse.previousPosition.y = gameState->screen.height / 2;
    gameState->camera.lx = sin( gameState->camera.angle_H );
    gameState->camera.ly = -tan( gameState->camera.angle_V );
    // pr_debug( "Vector_Y:%f", gameState->camera.ly );

    gameState->camera.lz = -cos( gameState->camera.angle_H );
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

    pr_debug( "Screen Size Change:%dx%d", w, h );
    globalGameState.screen.width = w;
    globalGameState.screen.height = h;

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
    gluPerspective( 45, ratio, CAMERA_SIZE, 1000 );

    // Get Back to the Modelview
    glMatrixMode( GL_MODELVIEW );
}

void showErrors( ) {
    int errCode;
    const GLubyte *errString;
    if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {
        errString = gluErrorString( errCode );
        pr_debug( "GL Error:%d:%s", errCode, errString );
    }
}

double fps_ms = ( 1.0 / 60.0 ) * 1000.0;

int main( int argc, char **argv ) {
    glutInit( &argc, argv );
    // glutInitContextVersion( 3, 1 );
    // glutInitContextFlags( GLUT_DEBUG );
    glutInitDisplayMode( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA );

    glutCreateWindow( "RepGame" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );

    if ( glewInit( ) ) {
        pr_debug( "GLEW init failed" );
        exit( 1 ); // or handle the error in a nicer way
    }
    if ( !GLEW_VERSION_2_1 ) { // check that the machine supports the 2.1 API.
        pr_debug( "GLEW version wrong" );

        exit( 1 ); // or handle the error in a nicer way
    }

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
    initilizeGameState( &globalGameState );
    textures_populate( );
    g_program = shaders_compile( );

    struct timespec tstart = {0, 0}, tend = {0, 0};
    clock_gettime( CLOCK_MONOTONIC, &tstart );
    tend = tstart;
    createDisplayList( );
    while ( !globalGameState.input.exitGame ) {
        glutMainLoopEvent( );
        // input_set_enable_mouse( 0 );
        glutWarpPointer( globalGameState.screen.width / 2, globalGameState.screen.height / 2 );
        // glutMainLoopEvent( );
        // input_set_enable_mouse( 1 );
        gameTick( &globalGameState );
        display( &globalGameState );
        showErrors( );

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
    glutLeaveMainLoop( );
    return 0;
}