#include <math.h>
#include <time.h>
#include <unistd.h>

#include "RepGame.h"

#define SKY_BOX_DISTANCE DRAW_DISTANCE * 0.8

void showErrors( ) {
    int errCode;
    const GLubyte *errString;
    if ( ( errCode = glGetError( ) ) != GL_NO_ERROR ) {
        errString = gluErrorString( errCode );
        pr_debug( "GL Error:%d:%s", errCode, errString );
    }
}

// // Returns the worls coord from the screen coords xy
// void getPosFromMouse( int x, int y, TRIP_ARGS( double *out_ ) ) {
//     GLint viewport[ 4 ];
//     GLdouble modelview[ 16 ];
//     GLdouble projection[ 16 ];
//     GLfloat winX, winY, winZ;
//     GLdouble posX, posY, posZ;
//     glGetDoublev( GL_MODELVIEW_MATRIX, modelview );
//     glGetDoublev( GL_PROJECTION_MATRIX, projection );
//     glGetIntegerv( GL_VIEWPORT, viewport );
//     winX = ( float )x;
//     winY = ( float )viewport[ 3 ] - ( float )y;
//     glReadPixels( x, ( int )winY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &winZ );

//     gluUnProject( winX, winY, winZ, modelview, projection, viewport, out_x, out_y, out_z );
// }

// static void calculateMousePos( int arg, TRIP_ARGS( int *out_create_ ), TRIP_ARGS( int *out_destroy_ ) ) {
//     double world_mouse_x, world_mouse_y, world_mouse_z = 0;
//     getPosFromMouse( globalGameState.screen.width / 2, globalGameState.screen.height / 2, &world_mouse_x, &world_mouse_y, &world_mouse_z );
//     int camera_x_round = round( globalGameState.camera.x );
//     int camera_y_round = round( globalGameState.camera.y );
//     int camera_z_round = round( globalGameState.camera.z );
//     // pr_debug( "Mouse x:%f y:%f z:%f", world_mouse_x, world_mouse_y, world_mouse_z );
//     int world_x_round = round( world_mouse_x );
//     int world_y_round = round( world_mouse_y );
//     int world_z_round = round( world_mouse_z );

//     double dis_x = world_x_round - world_mouse_x;
//     double dis_y = world_y_round - world_mouse_y;
//     double dis_z = world_z_round - world_mouse_z;

//     double dis_x_abs = fabs( dis_x );
//     double dis_y_abs = fabs( dis_y );
//     double dis_z_abs = fabs( dis_z );

//     int face_x = ( dis_x_abs < 0.01 );
//     int face_y = ( dis_y_abs < 0.01 );
//     int face_z = ( dis_z_abs < 0.01 );
//     // pr_debug( "Face x:%d y:%d z:%d", face_x, face_y, face_z );

//     double sig_x = ( round( world_mouse_x ) - floor( globalGameState.camera.x ) == 0 ) ? -1 : ( ( round( world_mouse_x ) - floor( globalGameState.camera.x ) ) / fabs( floor( globalGameState.camera.x ) - round( world_mouse_x ) ) );
//     double sig_y = ( round( world_mouse_y ) - floor( globalGameState.camera.y ) == 0 ) ? -1 : ( ( round( world_mouse_y ) - floor( globalGameState.camera.y ) ) / fabs( floor( globalGameState.camera.y ) - round( world_mouse_y ) ) );
//     double sig_z = ( round( world_mouse_z ) - floor( globalGameState.camera.z ) == 0 ) ? -1 : ( ( round( world_mouse_z ) - floor( globalGameState.camera.z ) ) / fabs( floor( globalGameState.camera.z ) - round( world_mouse_z ) ) );
//     // pr_debug( "Sig x:%+2f y:%+2f z:%+2f", sig_x, sig_y, sig_z );
//     // pr_debug( "Faceing x:%+2.0f y:%+2.0f z:%+2.0f", sig_x * face_x, sig_y * face_y, sig_z * face_z );

//     int offset_x = sig_x * face_x;
//     int offset_y = sig_y * face_y;
//     int offset_z = sig_z * face_z;

//     // int offset_x = face_x * ( world_x_round <= camera_x_round );
//     // int offset_y = face_y * ( world_y_round <= camera_y_round );
//     // int offset_z = face_z * ( world_z_round <= camera_z_round );
//     // pr_debug( " Offset x:%d y:%d z:%d", offset_x, offset_y, offset_z );

//     // int offset_x2 = ( world_x_round == camera_x_round ) && face_x;
//     // int offset_y2 = ( world_y_round == camera_y_round ) && face_y;
//     // int offset_z2 = ( world_z_round == camera_z_round ) && face_z;
//     // pr_debug( "Offse2 x:%d y:%d z:%d", offset_x2, offset_y2, offset_z2 );

//     int world_x = face_x ? world_x_round : floor( world_mouse_x );
//     int world_y = face_y ? world_y_round : floor( world_mouse_y );
//     int world_z = face_z ? world_z_round : floor( world_mouse_z );
//     // pr_debug( "World %d x:%d y:%d z:%d", arg, world_x, world_y, world_z );

//     world_x += ( offset_x == 1 ? 0 : offset_x );
//     world_y += ( offset_y == 1 ? 0 : offset_y );
//     world_z += ( offset_z == 1 ? 0 : offset_z );

//     *out_destroy_x = world_x;
//     *out_destroy_y = world_y;
//     *out_destroy_z = world_z;

//     *out_create_x = world_x - offset_x;
//     *out_create_y = world_y - offset_y;
//     *out_create_z = world_z - offset_z;
//     // The player has selected this block (unless there are edge problems)
// }

void arrowKeyDownInput( int key, int x, int y ) {
    repgame_arrowKeyDownInput( key, x, y );
}

void arrowKeyUpInput( int key, int x, int y ) {
    repgame_arrowKeyUpInput( key, x, y );
}

void mouseInput( int button, int state, int x, int y ) {
    repgame_mouseInput( button, state, x, y );
}

void keysInput( unsigned char key, int x, int y ) {
    repgame_keysInput( key, x, y, 1 );
}

void keysInputUp( unsigned char key, int x, int y ) {
    repgame_keysInput( key, x, y, 0 );
}

void mouseMove( int x, int y ) {
    repgame_mouseMove( x, y );
}

double fps_ms = ( 1.0 / FPS_LIMIT ) * 1000.0;

void changeSize( int w, int h ) {
    repgame_changeSize( w, h );
}

int main( int argc, char **argv ) {
    glutInit( &argc, argv );
    glutInitContextVersion( 3, 3 );
    // glutInitContextFlags( GLUT_DEBUG );
    glutInitDisplayMode( GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA );

    glutCreateWindow( "RepGame" );
    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );

    if ( glewInit( ) ) {
        pr_debug( "GLEW init failed" );
        exit( 1 ); // or handle the error in a nicer way
    }
    if ( !GLEW_VERSION_3_0 ) { // check that the machine supports the 2.1 API.
        pr_debug( "GLEW version wrong" );
        exit( 1 ); // or handle the error in a nicer way
    }

    repgame_init( );

    glutSpecialFunc( arrowKeyDownInput );
    glutSpecialUpFunc( arrowKeyUpInput );

    if ( LOCK_MOUSE ) {
        glutSetCursor( GLUT_CURSOR_NONE );
    }

    glutKeyboardFunc( keysInput );
    glutKeyboardUpFunc( keysInputUp );
    glutMouseFunc( mouseInput );
    glutReshapeFunc( changeSize );
    glutPassiveMotionFunc( mouseMove );
    glutMotionFunc( mouseMove );

    struct timespec tstart = {0, 0}, tend = {0, 0}, tblank = {0, 0};

    while ( !repgame_shouldExit( ) ) {
        clock_gettime( CLOCK_MONOTONIC, &tstart );
        glutMainLoopEvent( );
        // pr_debug( "Drawing" );
        int width, height;
        repgame_get_screen_size( &width, &height );
        if ( LOCK_MOUSE ) {
            glutWarpPointer( width / 2, height / 2 );
        }
        repgame_clear( );
        repgame_tick( );
        repgame_draw( );
        glutSwapBuffers( );

        showErrors( );

        clock_gettime( CLOCK_MONOTONIC, &tend );
        {
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
            pr_debug( "FPS:%f", frame_rate );
        }
    }
    repgame_cleanup( );
    glutLeaveMainLoop( );
    return 0;
}