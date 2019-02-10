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
extern "C" void testJS( int num, const char *string ) {
    pr_debug( "testJS called with:%d %s", num, string );
}

#define WASM_PAN_SENSITIVITY 2
void mouseMove( int x, int y ) {
    // pr_debug( "mouseMove %d %d", x, y );
    int current_screen_width, current_screen_height;
    repgame_get_screen_size( &current_screen_width, &current_screen_height );

    input_lookMove( repgame_getInputState( ), current_screen_width / 2 + x * WASM_PAN_SENSITIVITY, current_screen_height / 2 + y * WASM_PAN_SENSITIVITY );
    // input_lookMove( repgame_getInputState( ), x, y );
}

void changeSize( int w, int h ) {
    repgame_changeSize( w, h );
}

char *repgame_getShaderString( const char *filename ) {
    pr_debug( "RepGame WASM doesn't support shaders from file" );
    return "RepGame WASM doesn't support shaders from file";
}

static void displayFunc( void ) {
    repgame_clear( );
    repgame_tick( );
    repgame_draw( );
    glutPostRedisplay( );
}

extern "C" int main( int argc, char **argv ) {
    glutInit( &argc, argv );
    pr_debug( "Entering RepGameWASM Paul Main" );

    glutInitDisplayMode( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA | GLUT_MULTISAMPLE );

    glutCreateWindow( "RepGame" );

    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );

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
