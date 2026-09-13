#include <math.h>
#include <time.h>
#include <unistd.h>

#include <emscripten.h>

#include "common/RepGame.hpp"
#include "common/rep_tests.hpp"

static RepGameState *globalGameState;

void repgame_linux_process_sdl_events( RepGame &repgame ) {
    SDL_Event event;
    Input &input = repgame.getInputState( );
    // On mobile, SDL2 converts touch to mouse events. A touch-drag to look
    // around would be interpreted as holding left-click (mining blocks).
    // Track touch movement to distinguish tap (break block) from drag (look).
    static bool touch_active = false;
    static int touch_start_x = 0, touch_start_y = 0;
    static bool touch_moved = false;
    static bool tap_break_pending = false;
    const int TAP_THRESHOLD_PX = 10;
    // Clear any pending tap-break from the previous frame so left-click is
    // only held for one tick (enough to break one block).
    if ( tap_break_pending ) {
        input.mouseInput( SDL_BUTTON_LEFT, true );
        tap_break_pending = false;
    }
    while ( SDL_PollEvent( &event ) ) {
        bool handledMouse = false;
        bool handledKeyboard = false;
        // pr_debug( "Event: mouse:%d keyboard:%d", handledMouse, handledKeyboard );
        switch ( event.type ) {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if ( handledKeyboard ) {
                    break;
                }
                input.keysInput( event.key.keysym.sym, event.type == SDL_KEYDOWN );
                break;
            case SDL_WINDOWEVENT:
                if ( event.window.event == SDL_WINDOWEVENT_RESIZED ) {
                    repgame.changeSize( event.window.data1, event.window.data2 );
                } else if ( event.window.event == SDL_WINDOWEVENT_CLOSE ) {
                    input.quit( );
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if ( handledMouse ) {
                    break;
                }
                if ( event.button.which == SDL_TOUCH_MOUSEID ) {
                    // Touch-synthesized mouse event — apply tap-vs-drag logic.
                    if ( event.type == SDL_MOUSEBUTTONDOWN ) {
                        touch_active = true;
                        touch_start_x = event.button.x;
                        touch_start_y = event.button.y;
                        touch_moved = false;
                    } else { // SDL_MOUSEBUTTONUP
                        if ( touch_active ) {
                            touch_active = false;
                            if ( !touch_moved ) {
                                // Tap: forward as a brief left-click so the
                                // game breaks one block on the next tick.
                                input.mouseInput( SDL_BUTTON_LEFT, false );
                                tap_break_pending = true;
                            }
                            // Drag: don't forward — camera already moved.
                        }
                    }
                } else {
                    // Real mouse event (desktop WASM) — forward normally.
                    input.mouseInput( event.button.button, event.type == SDL_MOUSEBUTTONUP );
                }
                break;
            case SDL_MOUSEWHEEL:
                if ( handledMouse ) {
                    break;
                }
                input.mouseWheel( event.wheel.x, event.wheel.y );
                break;
            case SDL_MOUSEMOTION:
                if ( handledMouse ) {
                    break;
                }
                input.lookMove( event.motion.xrel, event.motion.yrel );
                input.mousePosition( event.motion.x, event.motion.y );
                if ( touch_active ) {
                    int dx = event.motion.x - touch_start_x;
                    int dy = event.motion.y - touch_start_y;
                    if ( dx * dx + dy * dy > TAP_THRESHOLD_PX * TAP_THRESHOLD_PX ) {
                        touch_moved = true;
                    }
                }
                break;
            default:
                break;
        }
    }
}

bool startsWith( const char *pre, const char *str ) {
    return strncmp( pre, str, strlen( pre ) ) == 0;
}

void main_loop_wasm( void *arg );

static SDL_Window *sdl_window = nullptr;
static SDL_GLContext sdl_context = nullptr;
int repgame_sdl2_main( const char *world_path, const char *host, const bool connect_multi, const bool tests ) {
    if ( tests ) {
        return rep_tests_start( );
    }
    if ( SDL_Init( SDL_INIT_VIDEO ) != 0 ) {    /* Initialize SDL's Video subsystem */
        pr_debug( "Unable to initialize SDL" ); /* Or die on error */
        exit( 1 );
    }

    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );

    /* Turn on double buffering with a 24bit Z buffer.
     * You may need to change this to 16 or 32 for your system */
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

    int default_width = DEFAULT_WINDOW_WIDTH;
    int default_height = DEFAULT_WINDOW_HEIGHT;
    // SDL_WINDOW_FULLSCREEN_DESKTOP triggers an unaligned atomic access in
    // SDL2's emscripten backend when pthreads are enabled. On WASM the canvas
    // is already sized to the full window via CSS in index.js, so fullscreen
    // is redundant there.
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    int window_x = SDL_WINDOWPOS_CENTERED;
    int window_y = SDL_WINDOWPOS_CENTERED;
    sdl_window = SDL_CreateWindow( "RepGame", window_x, window_y, default_width, default_height, window_flags );
    if ( !sdl_window ) {
        pr_debug( "Creating the SDL window failed" );
        exit( 1 );
    }
    sdl_context = SDL_GL_CreateContext( sdl_window );
    if ( !sdl_context ) {
        pr_debug( "Creating the SDL context failed" );
        exit( 1 );
    }

    const char *opengl_version = reinterpret_cast<const char *>( glGetString( GL_VERSION ) );
    pr_debug( "Using OpenGL Version:%s", opengl_version );
    const char *opengl_renderer = reinterpret_cast<const char *>( glGetString( GL_RENDERER ) );
    const bool supportsAnisotropicFiltering = !startsWith( "llvmpipe", opengl_renderer );
    pr_debug( "Using OpenGL Renderer:%s", opengl_renderer );
    const char *opengl_vendor = reinterpret_cast<const char *>( glGetString( GL_VENDOR ) );
    pr_debug( "Using OpenGL Vendor:%s", opengl_vendor );

    glewExperimental = GL_TRUE;
    if ( glewInit( ) ) {
        pr_debug( "GLEW init failed" );
        exit( 1 ); // or handle the error in a nicer way
    }
    ignoreErrors( );
    RepGame *repgamePrt = new RepGame( );
    RepGame &repgame = *repgamePrt;
    globalGameState = repgame.init( world_path, connect_multi, host, supportsAnisotropicFiltering );

    emscripten_set_main_loop_arg( main_loop_wasm, ( void * )&repgame, 0, 1 );
    return 0;
}

int is_locking_pointer = 0;
void repgame_linux_process_window_and_pointer_state( const RepGame &repgame ) {
    int width, height;
    const bool should_lock_pointer = repgame.should_lock_pointer( );
    // Sync the game's expected pointer lock state to JS every frame. The
    // pointerlockchange listener in index.js checks this to distinguish
    // game-initiated unlocks (e.g. opening inventory) from ESC presses.
    EM_ASM_INT( { gameWantsPointerLock = $0; return 0; }, should_lock_pointer ? 1 : 0 );
    if ( should_lock_pointer != is_locking_pointer ) {
#if ALLOW_GRAB_MOUSE
        SDL_SetRelativeMouseMode( should_lock_pointer ? SDL_TRUE : SDL_FALSE );
#endif
        SDL_GetWindowSize( sdl_window, &width, &height );
        SDL_WarpMouseInWindow( sdl_window, width / 2, height / 2 );
        is_locking_pointer = should_lock_pointer;
    }
}

void main_loop_wasm( void *arg ) {
    RepGame &repgame = *static_cast<RepGame *>( arg );
    if ( repgame.shouldExit( ) ) {
        // Show the exit screen and stop the main loop so the browser can
        // handle the page normally (instead of leaving a black canvas).
        EM_ASM( show_exit_screen( ); );
        emscripten_cancel_main_loop( );
        return;
    } else {
        repgame_linux_process_sdl_events( repgame );
        repgame_linux_process_window_and_pointer_state( repgame );
        repgame.clear( );
        repgame.tick( );
        repgame.draw( 1.0f );
    }
    return;
}
