#include <math.h>
#include <time.h>
#include <unistd.h>

#if defined( REPGAME_WASM )
#include <emscripten.h>
#endif

#include "common/RepGame.hpp"
#include "common/rep_tests.hpp"

static RepGameState *globalGameState;

void repgame_linux_process_sdl_events( RepGame &repgame ) {
    SDL_Event event;
    Input &input = repgame.getInputState( );
#if defined( REPGAME_WASM )
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
#endif
    while ( SDL_PollEvent( &event ) ) {
        bool handledMouse = false;
        bool handledKeyboard = false;
#if SUPPORTS_IMGUI_OVERLAY
        imgui_overlay_handle_sdl2_event( &globalGameState->imgui_overlay, &event, &handledMouse, &handledKeyboard );
#endif
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
#if defined( REPGAME_WASM )
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
#else
                input.mouseInput( event.button.button, event.type == SDL_MOUSEBUTTONUP );
#endif
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
#if defined( REPGAME_WASM )
                if ( touch_active ) {
                    int dx = event.motion.x - touch_start_x;
                    int dy = event.motion.y - touch_start_y;
                    if ( dx * dx + dy * dy > TAP_THRESHOLD_PX * TAP_THRESHOLD_PX ) {
                        touch_moved = true;
                    }
                }
#endif
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
void main_loop_full( RepGame &repgame );

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
#if !defined( REPGAME_WASM )
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
    // next line as per: http://stackoverflow.com/questions/11961116/opengl-3-x-context-creation-using-sdl2-on-osx-macbook-air-2012
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
#endif

    // I use glDrawElementsInstanced( GL_LINES, ...) which isn't allowed anymore... but it makes a nice crosshair.
    // SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG );

    /* Turn on double buffering with a 24bit Z buffer.
     * You may need to change this to 16 or 32 for your system */
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );

#if SUPPORTS_FRAME_BUFFER
    // MSAA happens in the FBO (see MSAA_SAMPLES in constants.hpp), so the
    // default framebuffer doesn't need it. Requesting MSAA here caused some
    // drivers to allocate 2x MSAA anyway, which produces flashing/edge
    // artifacts in the ImGui overlay (its semi-transparent window background
    // resolves differently per sample at swap time). glDisable(GL_MULTISAMPLE)
    // only disables per-fragment rasterization, not the multisampled storage
    // or the swap-time resolve, so it doesn't fix the glitching. Explicitly
    // request 0 samples. LOW mode loses edge AA on Linux/Windows as a result.
    SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
    SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );
#endif
    int default_width = DEFAULT_WINDOW_WIDTH;
    int default_height = DEFAULT_WINDOW_HEIGHT;
    // SDL_WINDOW_FULLSCREEN_DESKTOP triggers an unaligned atomic access in
    // SDL2's emscripten backend when pthreads are enabled. On WASM the canvas
    // is already sized to the full window via CSS in index.js, so fullscreen
    // is redundant there.
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#if !defined( REPGAME_WASM )
    window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    sdl_window = SDL_CreateWindow( "RepGame", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, default_width, default_height, window_flags );
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
#if SUPPORTS_IMGUI_OVERLAY
    imgui_overlay_attach_to_window( &globalGameState->imgui_overlay, sdl_window, sdl_context );
#endif

#if !defined( REPGAME_WASM )

#ifdef REPGAME_HW_VSYNC
    SDL_GL_SetSwapInterval( 1 );
    // pr_debug( "Limiting FPS" );
#else
    SDL_GL_SetSwapInterval( 0 );
    // pr_debug( "Unlimited FPS" );
#endif

    if ( !GLEW_VERSION_3_3 ) { // check that the machine supports the OpenGL 3.3
        pr_debug( "GLEW version wrong" );
        exit( 1 ); // or handle the error in a nicer way
    }
    repgame.changeSize( default_width, default_height );
    main_loop_full( repgame );
    repgame.cleanup( );
    delete repgamePrt;
    SDL_GL_DeleteContext( sdl_context );
    SDL_DestroyWindow( sdl_window );
    SDL_Quit( );
#else
    emscripten_set_main_loop_arg( main_loop_wasm, ( void * )&repgame, 0, 1 );
#endif
    return 0;
}

int is_locking_pointer = 0;
void repgame_linux_process_window_and_pointer_state( const RepGame &repgame ) {
    int width, height;
    const bool should_lock_pointer = repgame.should_lock_pointer( );
#if defined( REPGAME_WASM )
    // Sync the game's expected pointer lock state to JS every frame. The
    // pointerlockchange listener in index.js checks this to distinguish
    // game-initiated unlocks (e.g. opening inventory) from ESC presses.
    EM_ASM_INT( { gameWantsPointerLock = $0; return 0; }, should_lock_pointer ? 1 : 0 );
#endif
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
#if defined( REPGAME_WASM )
        EM_ASM( show_exit_screen( ); );
        emscripten_cancel_main_loop( );
#endif
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

#ifdef REPGAME_SW_VSYNC
#define SW_VSYNC_ENABLED 1
#else
#define SW_VSYNC_ENABLED 0
#endif

void main_loop_full( RepGame &repgame ) {

    constexpr int time_step_ms = 1000 / UPS_RATE;
    long next_game_step = SDL_GetTicks64( ); // initial value

    long fps_last_report_time = SDL_GetTicks64( );
    long fps_frame_count = 0;

    while ( !repgame.shouldExit( ) ) {
        const long now = SDL_GetTicks64( );

        if ( ( ( next_game_step - now ) <= 0 ) || !SW_VSYNC_ENABLED ) {
            int computer_is_too_slow_limit = 10; // max number of advances per render, if you can't get 20 fps, slow the game's UPS

            // Loop until all steps are executed or computer_is_too_slow_limit is reached
            // int num_ticks_in_frame = 0;
            while ( ( ( ( next_game_step - now ) <= 0 ) ) && ( computer_is_too_slow_limit-- ) ) {
                repgame_linux_process_sdl_events( repgame );
                repgame_linux_process_window_and_pointer_state( repgame );
                repgame.tick( );
                // num_ticks_in_frame++;
                next_game_step += time_step_ms; // count 1 game tick done
            }
            // pr_debug( "slow:%d num_ticks_in_frame:%d fps:%f", computer_is_too_slow_limit, num_ticks_in_frame, ( float )( UPS_RATE ) / ( float )num_ticks_in_frame );

            // Interpolation factor: how far we are between the last executed tick
            // (at next_game_step - time_step_ms) and the next scheduled tick (next_game_step).
            const long render_now = SDL_GetTicks64( );
            float alpha = static_cast<float>( render_now - ( next_game_step - time_step_ms ) ) / static_cast<float>( time_step_ms );
            if ( alpha < 0.0f ) {
                alpha = 0.0f;
            } else if ( alpha > 1.0f ) {
                alpha = 1.0f;
            }

            repgame.clear( );
            repgame.draw( alpha );
            SDL_GL_SwapWindow( sdl_window );

            fps_frame_count++;
            const long fps_now = SDL_GetTicks64( );
            const long fps_elapsed = fps_now - fps_last_report_time;
            if ( fps_elapsed >= 1000 ) {
                const float fps = static_cast<float>( fps_frame_count * 1000 ) / static_cast<float>( fps_elapsed );
                pr_debug( "FPS: %.1f", fps );
                fps_frame_count = 0;
                fps_last_report_time = fps_now;
            }
        } else {
            SDL_Delay( next_game_step - now );
        }
    }
}