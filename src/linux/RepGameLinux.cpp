#include <math.h>
#include <time.h>
#include <unistd.h>

#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"

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

void mouseMove( int x, int y ) {
    input_lookMove( repgame_getInputState( ), x, y );
}

double fps_ms = ( 1.0 / FPS_LIMIT ) * 1000.0;

void changeSize( int w, int h ) {
    repgame_changeSize( w, h );
}

char *repgame_getShaderString( const char *filename ) {
    return ( char * )"RepGame Linux doesn't support shaders from file";
}

int main( int argc, char **argv ) {
    const char *world_path;
    const char *host;
    bool connect_multi = true;

    if ( argc == 1 ) {
        world_path = "World1";
    } else if ( argc == 2 ) {
        world_path = argv[ 1 ];
        host = "repkap11.com";
    } else if ( argc == 3 ) {
        world_path = argv[ 1 ];
        host = argv[ 2 ];
    } else {
        pr_debug( "\nusage: %s world_path\n\tWhere world_path is relitive to the executable.", argv[ 0 ] );
        return -1;
    }
    glutInit( &argc, argv );
    glutInitContextVersion( 3, 3 );
    glutInitContextProfile( GLUT_CORE_PROFILE );

    // glutInitContextFlags( GLUT_DEBUG );
    glutSetOption( GLUT_MULTISAMPLE, 16 );
    glutInitDisplayMode( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA | GLUT_MULTISAMPLE );

    glutInitWindowSize( glutGet( GLUT_SCREEN_WIDTH ), glutGet( GLUT_SCREEN_HEIGHT ) );
    glutInitWindowPosition( 0, 0 );

    int glut_window = glutCreateWindow( "RepGame" );

    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );

    if ( glewInit( ) ) {
        pr_debug( "GLEW init failed" );
        exit( 1 ); // or handle the error in a nicer way
    }
    if ( !GLEW_VERSION_3_3 ) { // check that the machine supports the 2.1 API.
        pr_debug( "GLEW version wrong" );
        exit( 1 ); // or handle the error in a nicer way
    }

    repgame_init( world_path );

    // Start broadcasting chunk updates
    if ( connect_multi ) {
        multiplayer_init( host, 25566 );
        // multiplayer_init( "localhost", 25566 );
    }

    glutSpecialFunc( arrowKeyDownInput );
    glutSpecialUpFunc( arrowKeyUpInput );

    glutKeyboardFunc( keysInput );
    glutKeyboardUpFunc( keysInputUp );
    glutMouseFunc( mouseInput );
    glutReshapeFunc( changeSize );
    glutPassiveMotionFunc( mouseMove );
    glutMotionFunc( mouseMove );

    struct timespec tstart = {0, 0}, tend = {0, 0}, tblank = {0, 0};
    // pr_debug( "Size of Int:%lu", sizeof( int ) );
    int is_locking_pointer = 0;
    while ( !repgame_shouldExit( ) ) {
        clock_gettime( CLOCK_MONOTONIC, &tstart );
        glutMainLoopEvent( );
        // pr_debug( "Drawing" );
        int width, height;
        repgame_get_screen_size( &width, &height );
        int should_lock_pointer = repgame_should_lock_pointer( );
        if ( should_lock_pointer != is_locking_pointer ) {
            if ( should_lock_pointer ) {
                glutSetCursor( GLUT_CURSOR_NONE );
            } else {
                glutSetCursor( GLUT_CURSOR_LEFT_ARROW );
            }
            is_locking_pointer = should_lock_pointer;
        }
        if ( should_lock_pointer ) {
            glutWarpPointer( width / 2, height / 2 );
        }
        repgame_clear( );
        repgame_tick( );
        repgame_draw( );
        glutSwapBuffers( );

        showErrors( );
        clock_gettime( CLOCK_MONOTONIC, &tend );
        if ( LIMIT_FPS ) {
            double diff_ms = ( ( ( double )tend.tv_sec + 1.0e-9 * tend.tv_nsec ) - ( ( double )tstart.tv_sec + 1.0e-9 * tstart.tv_nsec ) ) * 1000.0;
            float frame_rate = 1.0 / ( diff_ms / 1000.0 );
            // pr_debug( "FPS (if wasn't waiting):%f", frame_rate );
            double wait_time_ms = fps_ms - diff_ms;
            if ( wait_time_ms > 0 ) {
                int wait_time_us = ( int )( wait_time_ms * 1000.0 );
                usleep( wait_time_us );
            }
        }
        clock_gettime( CLOCK_MONOTONIC, &tblank );
        {
            double diff_ms = ( ( ( double )tend.tv_sec + 1.0e-9 * tblank.tv_nsec ) - ( ( double )tblank.tv_sec + 1.0e-9 * tstart.tv_nsec ) ) * 1000.0;
            float frame_rate = 1.0 / ( diff_ms / 1000.0 );
            if ( SHOW_FPS ) {
                static int show_fps = 0;
                show_fps++;
                if ( show_fps >= 200 ) {
                    show_fps = 0;
                    pr_debug( "FPS:%f", frame_rate );
                }
            }
        }
    }

    // Stop broadcasting chunk updates
    multiplayer_cleanup( );
    repgame_cleanup( );
    glutLeaveMainLoop( );
    glutDestroyWindow( glut_window );
    return 0;
}