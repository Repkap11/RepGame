#include <math.h>
#include <time.h>
#include <unistd.h>

#include "common/RepGame.hpp"

void repgame_linux_process_sdl_events( ) {
    SDL_Event event;
    while ( SDL_PollEvent( &event ) ) {
        switch ( event.type ) {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                input_keysInput( repgame_getInputState( ), event.key.keysym.sym, event.type == SDL_KEYDOWN );
                break;
            case SDL_WINDOWEVENT:
                if ( event.window.event == SDL_WINDOWEVENT_RESIZED ) {
                    repgame_changeSize( event.window.data1, event.window.data2 );
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                input_mouseInput( repgame_getInputState( ), event.button.button, event.type == SDL_MOUSEBUTTONUP );
                break;
            case SDL_MOUSEWHEEL:
                input_mouseWheel( repgame_getInputState( ), event.wheel.x, event.wheel.y );
                break;
            case SDL_MOUSEMOTION:
                input_lookMove( repgame_getInputState( ), event.motion.xrel, event.motion.yrel );
                break;
        }
    }
}

bool startsWith( const char *pre, const char *str ) {
    return strncmp( pre, str, strlen( pre ) ) == 0;
}

void main_loop_wasm( );
void main_loop_full( );

SDL_Window *sdl_window = NULL;
SDL_GLContext sdl_context = NULL;
int repgame_sdl2_main( const char *world_path, const char *host, bool connect_multi, bool tests ) {
    if ( SDL_Init( SDL_INIT_VIDEO ) != 0 ) {    /* Initialize SDL's Video subsystem */
        pr_debug( "Unable to initialize SDL" ); /* Or die on error */
        exit( 1 );
    }

    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
#if !defined( REPGAME_WASM )
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
    // next line as per: http://stackoverflow.com/questions/11961116/opengl-3-x-context-creation-using-sdl2-on-osx-macbook-air-2012
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
#endif

    /* Turn on double buffering with a 24bit Z buffer.
     * You may need to change this to 16 or 32 for your system */
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

#if SUPPORTS_FRAME_BUFFER
    SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 1 ); // Multi sample happens in FB
#endif
    int default_width = DEFAULT_WINDOW_WIDTH;
    int default_height = DEFAULT_WINDOW_HEIGHT;
    /* Create our window centered */
    sdl_window = SDL_CreateWindow( "RepGame", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, default_width, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED );
    if ( !sdl_window ) {
        pr_debug( "Creating the SDL window failed" );
        exit( 1 );
    }
    sdl_context = SDL_GL_CreateContext( sdl_window );
    if ( !sdl_context ) {
        pr_debug( "Creating the SDL context failed" );
        exit( 1 );
    }

    pr_debug( "Using OpenGL Version:%s", glGetString( GL_VERSION ) );
    const GLubyte *renderer = glGetString( GL_RENDERER );
    bool supportsAnisotropicFiltering = !startsWith( "llvmpipe", ( const char * )renderer );
    pr_debug( "Using OpenGL Renderer:%s", renderer );
    pr_debug( "Using OpenGL Vendor:%s", glGetString( GL_VENDOR ) );

    glewExperimental = GL_TRUE;
    if ( glewInit( ) ) {
        pr_debug( "GLEW init failed" );
        exit( 1 ); // or handle the error in a nicer way
    }
    ignoreErrors( );

    if ( tests ) {
        return rep_tests_start( );
    }
    RepGameState* globalGameState = repgame_init( world_path, connect_multi, host, supportsAnisotropicFiltering);
    imgui_overlay_attach_to_window(&globalGameState->imgui_overlay, sdl_window, sdl_context);

#if !defined( REPGAME_WASM )

    /* This makes our buffer swap syncronized with the monitor's vertical refresh */
#if defined( REPGAME_FAST )
    SDL_GL_SetSwapInterval( 0 );
#else
    SDL_GL_SetSwapInterval( 1 );
#endif

    if ( !GLEW_VERSION_3_3 ) { // check that the machine supports the 2.1 API.
        pr_debug( "GLEW version wrong" );
        exit( 1 ); // or handle the error in a nicer way
    }
    repgame_changeSize( default_width, default_height );
    main_loop_full( );
    repgame_cleanup( );
    SDL_GL_DeleteContext( sdl_context );
    SDL_DestroyWindow( sdl_window );
    SDL_Quit( );
#else
    emscripten_set_main_loop( main_loop_wasm, 0, 1 );
#endif
    return 0;
}

int is_locking_pointer = 0;
void repgame_linux_process_window_and_pointer_state( ) {
    int width, height;
    int should_lock_pointer = repgame_should_lock_pointer( );
    if ( should_lock_pointer != is_locking_pointer ) {
        SDL_SetRelativeMouseMode( should_lock_pointer ? SDL_TRUE : SDL_FALSE );
        SDL_GetWindowSize( sdl_window, &width, &height );
        SDL_WarpMouseInWindow( sdl_window, width / 2, height / 2 );
        is_locking_pointer = should_lock_pointer;
    }
}

void main_loop_wasm( ) {
    if ( repgame_shouldExit( ) ) {
        return;
    } else {
        repgame_linux_process_sdl_events( );
        repgame_linux_process_window_and_pointer_state( );
        repgame_clear( );
        repgame_tick( );
        repgame_draw( );
    }
    return;
}

#define BE_NICE_AND_DONT_BURN_THE_CPU 1

void main_loop_full( ) {
    int vsync_enabled = 1;

    Uint32 time_step_ms = 1000 / UPS_RATE;
    Uint32 next_game_step = SDL_GetTicks( ); // initial value

    while ( !repgame_shouldExit( ) ) {
        Uint32 now = SDL_GetTicks( );

        if ( ( ( ( int )next_game_step - ( int )now ) <= 0 ) || vsync_enabled ) {
            int computer_is_too_slow_limit = 10; // max number of advances per render, if you can't get 20 fps, slow the game's UPS

            // Loop until all steps are executed or computer_is_too_slow_limit is reached
            int num_ticks_in_frame = 0;
            while ( ( ( ( ( int )next_game_step - ( int )now ) <= 0 ) ) && ( computer_is_too_slow_limit-- ) ) {
                repgame_linux_process_sdl_events( );
                repgame_linux_process_window_and_pointer_state( );
                repgame_tick( );
                num_ticks_in_frame++;
                next_game_step += time_step_ms; // count 1 game tick done
            }
            // pr_debug( "slow:%d num_ticks_in_frame:%d fps:%f", computer_is_too_slow_limit, num_ticks_in_frame, ( float )( UPS_RATE ) / ( float )num_ticks_in_frame );

            repgame_clear( );
            repgame_draw( );
            SDL_GL_SwapWindow( sdl_window );
        } else {
            // we're too fast, wait a bit.
            if ( BE_NICE_AND_DONT_BURN_THE_CPU ) {
                SDL_Delay( next_game_step - now );
            }
        }
    }
}