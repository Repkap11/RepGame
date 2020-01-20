#include <math.h>
#include <time.h>
#include <unistd.h>

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

    int tests = 0;
    if ( argc == 1 ) {
        world_path = "World1";
        connect_multi = false;
        host = "";
    } else if ( argc == 2 ) {
        if ( strcmp( argv[ 1 ], "tests" ) == 0 ) {
            tests = 1;
        }
        world_path = argv[ 1 ];
        connect_multi = false;
        host = "";
    } else if ( argc == 3 ) {
        world_path = argv[ 1 ];
        host = argv[ 2 ];
    } else {
        pr_debug( "\nusage: %s world_path\n\tWhere world_path is relitive to the executable.", argv[ 0 ] );
        return -1;
    }
    // glutInit( &argc, argv );
    // glutInitContextVersion( 3, 3 );
    // glutInitContextProfile( GLUT_CORE_PROFILE );

    // glutInitContextFlags( GLUT_DEBUG );
    // glutSetOption( GLUT_MULTISAMPLE, 16 );
    // glutInitDisplayMode( GLUT_DEPTH | GLUT_STENCIL | GLUT_DOUBLE | GLUT_RGBA | GLUT_MULTISAMPLE );

    // glutInitWindowSize( glutGet( GLUT_SCREEN_WIDTH ), glutGet( GLUT_SCREEN_HEIGHT ) );
    // glutInitWindowPosition( 0, 0 );

    // int glut_window = glutCreateWindow( "RepGame" );
    SDL_Window *mainwindow;    /* Our window handle */
    SDL_GLContext maincontext; /* Our opengl context handle */

    if ( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {     /* Initialize SDL's Video subsystem */
        pr_debug( "Unable to initialize SDL" ); /* Or die on error */
        exit( 1 );
    }

    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );

    // SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE,  );
    // SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );

    // next line as per: http://stackoverflow.com/questions/11961116/opengl-3-x-context-creation-using-sdl2-on-osx-macbook-air-2012
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );

    /* Turn on double buffering with a 24bit Z buffer.
     * You may need to change this to 16 or 32 for your system */
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

    /* Create our window centered */
    mainwindow = SDL_CreateWindow( "RepGame", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 800, SDL_WINDOW_OPENGL );

    maincontext = SDL_GL_CreateContext( mainwindow );

    /* This makes our buffer swap syncronized with the monitor's vertical refresh */
    SDL_GL_SetSwapInterval( 1 );

    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    pr_debug( "Using OpenGL Renderer:%s", glGetString( GL_RENDERER ) );
    glewExperimental = GL_TRUE;
    if ( glewInit( ) ) {
        pr_debug( "GLEW init failed" );
        exit( 1 ); // or handle the error in a nicer way
    }
    ignoreErrors( );

    if ( !GLEW_VERSION_3_3 ) { // check that the machine supports the 2.1 API.
        pr_debug( "GLEW version wrong" );
        exit( 1 ); // or handle the error in a nicer way
    }

    if ( tests ) {
        return rep_tests_start( );
    }
    repgame_init( world_path, connect_multi, host );
    repgame_changeSize( 1600, 800 );

    // glutSpecialFunc( arrowKeyDownInput );
    // glutSpecialUpFunc( arrowKeyUpInput );

    // glutKeyboardFunc( keysInput );
    // glutKeyboardUpFunc( keysInputUp );
    // glutMouseFunc( mouseInput );
    // glutReshapeFunc( changeSize );
    // glutPassiveMotionFunc( mouseMove );
    // glutMotionFunc( mouseMove );

    // struct timespec tstart = {0, 0}, tend = {0, 0}, tblank = {0, 0};
    // pr_debug( "Size of Int:%lu", sizeof( int ) );
    int is_locking_pointer = 0;
    glViewport( 0, 0, 1000, 1000 );
    glClearColor( 1.f, 0.f, 1.f, 0.f );
    glClear( GL_COLOR_BUFFER_BIT );
    bool Running = true;
    while ( !repgame_shouldExit( ) && Running ) {
        // clock_gettime( CLOCK_MONOTONIC, &tstart );
        //     // glutMainLoopEvent( );
        pr_debug( "Drawing" );
        // SDL_GL_SwapWindow( mainwindow );
        SDL_Event Event;
        while ( SDL_PollEvent( &Event ) ) {
            if ( Event.type == SDL_KEYDOWN ) {
                switch ( Event.key.keysym.sym ) {
                    case SDLK_ESCAPE:
                        Running = 0;
                        break;
                    default:
                        Running = 1;
                }
            }
        }

        int width, height;
        repgame_get_screen_size( &width, &height );
        int should_lock_pointer = repgame_should_lock_pointer( );
        if ( should_lock_pointer != is_locking_pointer ) {
            if ( should_lock_pointer ) {
                // glutSetCursor( GLUT_CURSOR_NONE );
            } else {
                // glutSetCursor( GLUT_CURSOR_LEFT_ARROW );
            }
            is_locking_pointer = should_lock_pointer;
        }
        if ( should_lock_pointer ) {
            // glutWarpPointer( width / 2, height / 2 );
        }
        repgame_clear( );
        repgame_tick( );
        repgame_draw( );
        SDL_GL_SwapWindow( mainwindow );
        // glutSwapBuffers( );

        showErrors( );
        // clock_gettime( CLOCK_MONOTONIC, &tend );
        // if ( LIMIT_FPS ) {
        //     double diff_ms = ( ( ( double )tend.tv_sec + 1.0e-9 * tend.tv_nsec ) - ( ( double )tstart.tv_sec + 1.0e-9 * tstart.tv_nsec ) ) * 1000.0;
        //     // float frame_rate = 1.0 / ( diff_ms / 1000.0 );
        //     // pr_debug( "FPS (if wasn't waiting):%f", frame_rate );
        //     double wait_time_ms = fps_ms - diff_ms;
        //     if ( wait_time_ms > 0 ) {
        //         int wait_time_us = ( int )( wait_time_ms * 1000.0 );
        //         usleep( wait_time_us );
        //     }
        // }
        // clock_gettime( CLOCK_MONOTONIC, &tblank );
        // {
        //     double diff_ms = ( ( ( double )tend.tv_sec + 1.0e-9 * tblank.tv_nsec ) - ( ( double )tblank.tv_sec + 1.0e-9 * tstart.tv_nsec ) ) * 1000.0;
        //     float frame_rate = 1.0 / ( diff_ms / 1000.0 );
        //     if ( SHOW_FPS ) {
        //         static int show_fps = 0;
        //         show_fps++;
        //         if ( show_fps >= 200 ) {
        //             show_fps = 0;
        //             pr_debug( "FPS:%f", frame_rate );
        //         }
        //     }
        // }
    }
    repgame_cleanup( );
    SDL_GL_DeleteContext( maincontext );
    SDL_DestroyWindow( mainwindow );
    SDL_Quit( );
    // glutLeaveMainLoop( );
    // glutDestroyWindow( glut_window );
    return 0;
}