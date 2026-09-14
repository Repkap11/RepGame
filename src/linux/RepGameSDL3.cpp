#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/rep_tests.hpp"
#ifdef REPGAME_LINUX
#include "linux/wayland_cursor_warp.hpp"
#endif

static RepGameState *globalGameState;

void repgame_linux_process_sdl_events( RepGame &repgame ) {
    SDL_Event event;
    Input &input = repgame.getInputState( );
    while ( SDL_PollEvent( &event ) ) {
        bool handledMouse = false;
        bool handledKeyboard = false;
#if SUPPORTS_IMGUI_OVERLAY
        imgui_overlay_handle_sdl2_event( &globalGameState->imgui_overlay, &event, &handledMouse, &handledKeyboard );
#endif
        switch ( event.type ) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if ( handledKeyboard ) {
                    break;
                }
                input.keysInput( event.key.key, event.type == SDL_EVENT_KEY_DOWN );
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                repgame.changeSize( event.window.data1, event.window.data2 );
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                input.quit( );
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if ( handledMouse ) {
                    break;
                }
                input.mouseInput( event.button.button, event.type == SDL_EVENT_MOUSE_BUTTON_UP );
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if ( handledMouse ) {
                    break;
                }
                input.mouseWheel( static_cast<int>( event.wheel.x ), static_cast<int>( event.wheel.y ) );
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if ( handledMouse ) {
                    break;
                }
                input.lookMove( static_cast<int>( event.motion.xrel ), static_cast<int>( event.motion.yrel ) );
                input.mousePosition( static_cast<int>( event.motion.x ), static_cast<int>( event.motion.y ) );
                break;
            default:
                break;
        }
    }
}

bool startsWith( const char *pre, const char *str ) {
    return strncmp( pre, str, strlen( pre ) ) == 0;
}

void main_loop_full( RepGame &repgame );

static SDL_Window *sdl_window = nullptr;
static SDL_GLContext sdl_context = nullptr;
int repgame_sdl2_main( const char *world_path, const char *host, const bool connect_multi, const bool tests ) {
    if ( tests ) {
        return rep_tests_start( );
    }
    // Video driver hint is set in main() (RepGameLinux.cpp) before the first
    // SDL_Init, since SDL_Init is idempotent and locks in the driver early.
    if ( !SDL_Init( SDL_INIT_VIDEO ) ) {    /* Initialize SDL's Video subsystem */
        pr_debug( "Unable to initialize SDL" ); /* Or die on error */
        exit( 1 );
    }
    pr_debug( "SDL video driver: %s (hint=%s)", SDL_GetCurrentVideoDriver( ), SDL_GetHint( SDL_HINT_VIDEO_DRIVER ) );

    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
    // next line as per: http://stackoverflow.com/questions/11961116/opengl-3-x-context-creation-using-sdl2-on-osx-macbook-air-2012
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );

    // The block selection outline used to use glDrawElementsInstanced( GL_LINES, ...)
    // with glLineWidth, which is deprecated in OpenGL 3.3+ core profile. It has been
    // replaced with a screen-space wireframe shader using GL_TRIANGLES, so the
    // forward-compatible flag can now be enabled to enforce a strict core profile.
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG );

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
    Uint64 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN;

    // SDL3's SDL_GetPrimaryDisplay() queries GNOME DBus / picks the best
    // display on Wayland (where there's no native "primary" concept). Allow
    // overriding via REPGAME_DISPLAY env var (0-based index into display list).
    SDL_DisplayID display_id = SDL_GetPrimaryDisplay( );
    if ( const char *display_env = getenv( "REPGAME_DISPLAY" ) ) {
        int parsed = atoi( display_env );
        int count = 0;
        const SDL_DisplayID *displays = SDL_GetDisplays( &count );
        if ( parsed >= 0 && parsed < count ) {
            display_id = displays[ parsed ];
        } else {
            pr_debug( "REPGAME_DISPLAY=%s is invalid (have %d displays), using primary", display_env, count );
        }
    }
    int window_x = SDL_WINDOWPOS_CENTERED_DISPLAY( display_id );
    int window_y = SDL_WINDOWPOS_CENTERED_DISPLAY( display_id );

    sdl_window = SDL_CreateWindow( "RepGame", default_width, default_height, window_flags );
    if ( !sdl_window ) {
        pr_debug( "Creating the SDL window failed" );
        exit( 1 );
    }
    SDL_SetWindowPosition( sdl_window, window_x, window_y );
    pr_debug( "Window on display: %d", SDL_GetDisplayForWindow( sdl_window ) );
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
    if ( GLenum glew_err = glewInit( ) ) {
        // glewInit() calls glxewInit() on Unix, which fails with
        // GLEW_ERROR_NO_GLX_DISPLAY (4) when SDL created an EGL context
        // (e.g. the Wayland video driver). The core GL function pointers
        // are already loaded by that point (glewContextInit succeeded), so
        // this particular error is harmless — the game doesn't use GLX.
        if ( glew_err != GLEW_ERROR_NO_GLX_DISPLAY ) {
            pr_debug( "GLEW init failed: %s (driver=%s)", glewGetErrorString( glew_err ), SDL_GetCurrentVideoDriver( ) );
            exit( 1 ); // or handle the error in a nicer way
        }
        pr_debug( "GLEW init: GLX unavailable on %s, using EGL-loaded core functions", SDL_GetCurrentVideoDriver( ) );
    }
    ignoreErrors( );
    RepGame *repgamePrt = new RepGame( );
    RepGame &repgame = *repgamePrt;
    globalGameState = repgame.init( world_path, connect_multi, host, supportsAnisotropicFiltering );
#if SUPPORTS_IMGUI_OVERLAY
    imgui_overlay_attach_to_window( &globalGameState->imgui_overlay, sdl_window, sdl_context );
#endif

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
    SDL_GL_DestroyContext( sdl_context );
    SDL_DestroyWindow( sdl_window );
    SDL_Quit( );
    return 0;
}

int is_locking_pointer = 0;
void repgame_linux_process_window_and_pointer_state( RepGame &repgame ) {
    int width, height;
    const bool should_lock_pointer = repgame.should_lock_pointer( );
    if ( should_lock_pointer != is_locking_pointer ) {
        SDL_GetWindowSize( sdl_window, &width, &height );
        const int center_x = width / 2;
        const int center_y = height / 2;
#if ALLOW_GRAB_MOUSE
        if ( should_lock_pointer ) {
            // Closing inventory → entering gameplay: warp to center, then
            // enable relative mode. On X11 this centers the cursor; on
            // Wayland the warp may fail but relative mode hides the cursor
            // anyway, so the visual result is the same.
            SDL_WarpMouseInWindow( sdl_window, static_cast<float>( center_x ), static_cast<float>( center_y ) );
            SDL_SetWindowRelativeMouseMode( sdl_window, true );
        } else {
            // Opening inventory → leaving gameplay: exit relative mode
            // first (SDL destroys its zwp_locked_pointer_v1), then warp.
            SDL_SetWindowRelativeMouseMode( sdl_window, false );
#ifdef REPGAME_LINUX
            // On GNOME Wayland, SDL_WarpMouseInWindow uses wp_pointer_warp_v1
            // which Mutter only honors while a mouse button is held, so it
            // silently fails. Use the zwp_pointer_constraints_v1 lock→hint→
            // unlock approach directly: Mutter warps the cursor to the hint
            // position when the one-shot lock is destroyed.
            {
                SDL_PropertiesID props = SDL_GetWindowProperties( sdl_window );
                struct wl_display *wl_dpy = static_cast<struct wl_display *>( SDL_GetPointerProperty( props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr ) );
                struct wl_surface *wl_surf = static_cast<struct wl_surface *>( SDL_GetPointerProperty( props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr ) );
                if ( !wayland_warp_cursor( wl_dpy, wl_surf, center_x, center_y ) )
#endif
                {
                    // Not Wayland, no pointer constraints support, or non-Linux:
                    // use SDL warp (works on X11 and Windows).
                    SDL_WarpMouseInWindow( sdl_window, static_cast<float>( center_x ), static_cast<float>( center_y ) );
                }
#ifdef REPGAME_LINUX
            }
#endif
        }
#endif
        // Warping does not always generate a MOUSE_MOTION event, so update
        // the inventory's absolute click position manually.
        repgame.getInputState( ).mousePosition( center_x, center_y );
        is_locking_pointer = should_lock_pointer;
    }
}

#ifdef REPGAME_SW_VSYNC
#define SW_VSYNC_ENABLED 1
#else
#define SW_VSYNC_ENABLED 0
#endif

void main_loop_full( RepGame &repgame ) {

    constexpr int time_step_ms = 1000 / UPS_RATE;
    long next_game_step = static_cast<long>( SDL_GetTicks( ) ); // initial value

    long fps_last_report_time = static_cast<long>( SDL_GetTicks( ) );
    long fps_frame_count = 0;

    while ( !repgame.shouldExit( ) ) {
        const long now = static_cast<long>( SDL_GetTicks( ) );

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
            const long render_now = static_cast<long>( SDL_GetTicks( ) );
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
            const long fps_now = static_cast<long>( SDL_GetTicks( ) );
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
