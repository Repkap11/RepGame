#include <math.h>
#include <time.h>
#include <unistd.h>
#include <GL/glut.h>

#include "common/RepGame.hpp"

#define SKY_BOX_DISTANCE DRAW_DISTANCE * 0.8

void arrowKeyDownInput( int key, int x, int y ) {
    input_arrowKeyDownInput( repgame_getInputState( ), key, x, y );
}

void arrowKeyUpInput( int key, int x, int y ) {
    input_arrowKeyUpInput( repgame_getInputState( ), key, x, y );
}

void mouseInput( int button, int state, int x, int y ) {
    input_mouseInput( repgame_getInputState( ), button, state, x, y );
}

void keysInput( unsigned char key, int x, int y ) {
    input_keysInput( repgame_getInputState( ), key, x, y, 1 );
}

void keysInputUp( unsigned char key, int x, int y ) {
    input_keysInput( repgame_getInputState( ), key, x, y, 0 );
}

#define WASM_PAN_SENSITIVITY 2
void mouseMove( int x, int y ) {
    // pr_debug( "mouseMove %d %d", x, y );
    int current_screen_width, current_screen_height;
    repgame_get_screen_size( &current_screen_width, &current_screen_height );

    input_lookMove( repgame_getInputState( ), current_screen_width / 2 + x * WASM_PAN_SENSITIVITY, current_screen_height / 2 + y * WASM_PAN_SENSITIVITY );
    // input_lookMove( repgame_getInputState( ), x, y );
}

double fps_ms = ( 1.0 / FPS_LIMIT ) * 1000.0;

void changeSize( int w, int h ) {
    repgame_changeSize( w, h );
}

const char *repgame_getShaderString( const char *filename ) {
    pr_debug( "RepGame WASM doesn't support shaders from file" );
    return "RepGame WASM doesn't support shaders from file";
}

static void displayFunc( void ) {
    // pr_debug( "RepGameWASM displayFunc" );
    repgame_clear( );
    repgame_tick( );
    repgame_draw( );
    // glutSwapBuffers( );
    glutPostRedisplay( );
    // pr_debug( "RepGameWASM displayFunc done" );
}

int main( int argc, char **argv ) {
    glutInit( &argc, argv );
    // glutInitContextVersion( 3, 3 );
    // glutInitWindowSize( 300, 300 );

    pr_debug( "Entering RepGameWASM" );
    // exit(0);

    // glutInitContextFlags( GLUT_DEBUG );
    // glutSetOption( GLUT_MULTISAMPLE, 16 );
    glutInitDisplayMode( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA | GLUT_MULTISAMPLE );

    glutCreateWindow( "RepGame" );

    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );

    // if ( glewInit( ) ) {
    //     pr_debug( "GLEW init failed" );
    //     exit( 1 ); // or handle the error in a nicer way
    // }
    // if ( !GLEW_VERSION_3_0 ) { // check that the machine supports the 2.1 API.
    //     pr_debug( "GLEW version wrong" );
    //     exit( 1 ); // or handle the error in a nicer way
    // }

    repgame_init( );

    glutSpecialFunc( arrowKeyDownInput );
    glutSpecialUpFunc( arrowKeyUpInput );

    if ( LOCK_MOUSE ) {
        // glutSetCursor( GLUT_CURSOR_NONE );
    }

    glutKeyboardFunc( keysInput );
    glutKeyboardUpFunc( keysInputUp );
    glutMouseFunc( mouseInput );
    glutReshapeFunc( changeSize );
    glutPassiveMotionFunc( mouseMove );
    glutMotionFunc( mouseMove );
    glutDisplayFunc( displayFunc );

    // repgame_get_screen_size( &width, &height );
    // int width, height;
    // if ( LOCK_MOUSE ) {
    //     // glutWarpPointer( width / 2, height / 2 );
    // }

    // glutSwapBuffers( );

    // showErrors( );
    pr_debug( "Starting glut main loop" );
    glutMainLoop( );
    pr_debug( "Glut main loop done?" );
    // while ( !repgame_shouldExit( ) ) {
    //     // glutMainLoopEvent( );
    //     // pr_debug( "Drawing" );
    // }
    repgame_cleanup( );
    // glutLeaveMainLoop( );
    return 0;
}