#include <math.h>
#include <time.h>
#include <unistd.h>

#include <emscripten.h>

#include "common/RepGame.hpp"
#include "common/rep_tests.hpp"
#include "common/map_gen.hpp"

static RepGameState *globalGameState;

// "Vsync" on WASM is the emscripten main-loop timing mode, not
// SDL_GL_SetSwapInterval (a no-op in the browser — presentation always rides
// the canvas composite). rAF locks each iteration to the display refresh;
// EM_TIMING_SETIMMEDIATE drives the loop via scheduler.postTask/postMessage,
// uncapped. The 'v' key toggles it via input.toggle_vsync.
static bool hw_vsync_enabled = true;

static void repgame_apply_vsync( ) {
    emscripten_set_main_loop_timing( hw_vsync_enabled ? EM_TIMING_RAF : EM_TIMING_SETIMMEDIATE, hw_vsync_enabled ? 1 : 0 );
}

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
    // Capture mode: ignore user input entirely (the camera is scripted), but
    // still honor window resize/close — same rule as the desktop loop.
    const bool capture = RepGame::capture_mode( );
    while ( SDL_PollEvent( &event ) ) {
        bool handledMouse = false;
        bool handledKeyboard = false;
#if SUPPORTS_IMGUI_OVERLAY
        if ( !capture ) {
            imgui_overlay_handle_sdl2_event( &globalGameState->imgui_overlay, &event, &handledMouse, &handledKeyboard );
        }
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
                    input.exitGame = true;
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
    if ( input.toggle_vsync ) {
        input.toggle_vsync = false;
        hw_vsync_enabled = !hw_vsync_enabled;
        repgame_apply_vsync( );
        pr_debug( "HW vsync: %s", hw_vsync_enabled ? "on (rAF)" : "off (setImmediate)" );
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

    // MSAA on the WebGL canvas: Emscripten's EGL layer
    // (emsdk/upstream/emscripten/src/lib/libegl.js) maps
    // EGL_SAMPLE_BUFFERS/EGL_SAMPLES to the WebGL `antialias` context
    // attribute; the browser picks the sample count (normally 4).
    // REPGAME_MSAA=0 disables it for A/B comparison.
    const char *msaa_env = getenv( "REPGAME_MSAA" );
    const bool msaa = !( msaa_env && atoi( msaa_env ) == 0 );
    SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0 );
    SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, msaa ? 4 : 0 );
    pr_debug( "MSAA requested: %s", msaa ? "on (4x)" : "off (REPGAME_MSAA=0)" );

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
#if SUPPORTS_IMGUI_OVERLAY
    imgui_overlay_attach_to_window( &globalGameState->imgui_overlay, sdl_window, sdl_context );
#endif
    // Same as the native init path: seed screen size/proj explicitly so the
    // first frame is correct even if no SDL_WINDOWEVENT_RESIZED arrives
    // (headless browsers may never fire one).
    repgame.changeSize( default_width, default_height );
    emscripten_set_main_loop_arg( main_loop_wasm, ( void * )&repgame, 0, 1 );
    return 0;
}

// Called from JS (index.js) when the user exits via ESC pointer-lock loss or
// the mobile back button — routes those exits through the normal C++ exit
// path so cleanup() saves the world before the page reloads.
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_request_exit( ) {
    if ( globalGameState ) {
        globalGameState->input.exitGame = true;
    }
}

// How often player.dat is rewritten during play, so a crash or tab close
// loses at most this much position/inventory progress. Chunk edits already
// persist continuously via the terrain loading threads.
#define WASM_SAVE_INTERVAL_MS 15000

// Freeze forensics: the browser main thread's wasm state is unreachable once
// it wedges (futex waits on the main thread busy-spin — Atomics.wait is
// disallowed there). This probe sits in shared wasm memory; a live pthread's
// JS context can fetch its address via wasmExports._repgame_main_probe_get()
// and read {frame, phase} from HEAP32 to see exactly which stage of
// main_loop_wasm the main thread is stuck in.
struct RepGameMainProbe {
    volatile int frame;
    volatile int phase;
};
static RepGameMainProbe repgame_main_probe;
extern "C" EMSCRIPTEN_KEEPALIVE RepGameMainProbe *repgame_main_probe_get( ) {
    return &repgame_main_probe;
}
enum {
    PHASE_IDLE = 0,
    PHASE_PUMP = 1,
    PHASE_SAVE = 2,
    PHASE_SDL_EVENTS = 3,
    PHASE_POINTER_STATE = 4,
    PHASE_TICK = 5,
    PHASE_CLEAR = 6,
    PHASE_DRAW = 7,
};

int is_locking_pointer = 0;
void repgame_linux_process_window_and_pointer_state( RepGame &repgame ) {
    int width, height;
    const bool should_lock_pointer = repgame.should_lock_pointer( );
    // Sync the game's expected pointer lock state to JS every frame. The
    // pointerlockchange listener in index.js checks this to distinguish
    // game-initiated unlocks (e.g. opening inventory) from ESC presses.
    EM_ASM_INT( { repgame_set_game_wants_pointer_lock( $0 ); return 0; }, should_lock_pointer ? 1 : 0 );
    if ( should_lock_pointer != is_locking_pointer ) {
        // Drop all button state on lock transitions. If a mouseup was ever
        // lost (e.g. swallowed by a native context menu or released outside
        // the window), the button would otherwise stay "held" forever.
        Input &input = repgame.getInputState( );
        input.mouse.buttons.left = 0;
        input.mouse.buttons.right = 0;
        input.mouse.buttons.middle = 0;
        input.mouse.buttons.left_click_handled = true;
        input.mouse.buttons.right_click_handled = true;
        input.mouse.buttons.middle_click_handled = true;
        input.mouse.buttons.left_pressed_in_ui = false;
        input.mouse.buttons.right_pressed_in_ui = false;
        input.mouse.buttons.middle_pressed_in_ui = false;
#if ALLOW_GRAB_MOUSE
        SDL_SetRelativeMouseMode( should_lock_pointer ? SDL_TRUE : SDL_FALSE );
#endif
        SDL_GetWindowSize( sdl_window, &width, &height );
        SDL_WarpMouseInWindow( sdl_window, width / 2, height / 2 );
        is_locking_pointer = should_lock_pointer;
    }
}

static inline long long now_us( ) {
    return static_cast<long long>( emscripten_get_now( ) * 1000.0 );
}

void main_loop_wasm( void *arg ) {
    repgame_main_probe.frame++;
    RepGame &repgame = *static_cast<RepGame *>( arg );
    if ( repgame.shouldExit( ) ) {
        // Release terrain workers parked on WebGPU mapgen jobs BEFORE
        // cleanup() joins them — job completion needs this thread's JS event
        // loop, which never runs again once cleanup blocks in pthread_join.
        webgpu_mapgen_shutdown( );
        // Save player.dat and persist all dirty chunks (writes go to the OPFS
        // backend and are durable on close), then show the exit screen and
        // stop the main loop so the browser can handle the page normally.
        repgame.cleanup( );
        EM_ASM( show_exit_screen( ); );
        emscripten_cancel_main_loop( );
        return;
    }
    // WebGPU mapgen pump: submits queued chunk jobs and picks up the async
    // init chain. Runs on the main thread because emdawnwebgpu's callbacks
    // resolve through this thread's JS event loop (once per rAF).
    repgame_main_probe.phase = PHASE_PUMP;
    webgpu_mapgen_pump( );

    static double last_save_ms = 0;
    const double now_ms = emscripten_get_now( );
    if ( now_ms - last_save_ms > WASM_SAVE_INTERVAL_MS ) {
        last_save_ms = now_ms;
        repgame_main_probe.phase = PHASE_SAVE;
        repgame.save_player_data( );
    }

    // Fixed-timestep accumulator, same shape as the desktop main_loop_full:
    // rAF fires once per display refresh, so on a 60Hz monitor each callback
    // owes UPS_RATE/60 (~2.4) ticks to keep the simulation at real-time speed.
    constexpr int time_step_ms = 1000 / UPS_RATE;
    static double next_game_step = -1.0;
    // Snap forward on the first frame and after long stalls — a backgrounded
    // tab stops rAF entirely, and on return the backlog would be thousands of
    // ticks that the 10-tick cap would fast-forward through for minutes.
    if ( next_game_step < 0.0 || now_ms - next_game_step > 2000.0 ) {
        next_game_step = now_ms;
    }

    // Events are polled inside the tick loop (as on desktop): per-tick polling
    // is what keeps tap_break_pending held for exactly one tick, and it avoids
    // processing input ahead of the simulation during catch-up.
    int computer_is_too_slow_limit = 10; // max advances per render; can't keep up -> game's UPS slows
    while ( next_game_step <= now_ms && computer_is_too_slow_limit-- ) {
        repgame_main_probe.phase = PHASE_SDL_EVENTS;
        repgame_linux_process_sdl_events( repgame );
        repgame_main_probe.phase = PHASE_POINTER_STATE;
        repgame_linux_process_window_and_pointer_state( repgame );
        repgame_main_probe.phase = PHASE_TICK;
        const long long t_tick = now_us( );
        repgame.tick( );
        repgame.profiling.us_tick = now_us( ) - t_tick;
        next_game_step += time_step_ms;
    }

    // Interpolation factor: how far we are between the last executed tick
    // (at next_game_step - time_step_ms) and the next scheduled tick.
    const double render_now_ms = emscripten_get_now( );
    float alpha = static_cast<float>( ( render_now_ms - ( next_game_step - time_step_ms ) ) / time_step_ms );
    if ( alpha < 0.0f ) {
        alpha = 0.0f;
    } else if ( alpha > 1.0f ) {
        alpha = 1.0f;
    }

    repgame_main_probe.phase = PHASE_CLEAR;
    repgame.clear( );
    repgame_main_probe.phase = PHASE_DRAW;
    repgame.draw( alpha );
    repgame_main_probe.phase = PHASE_IDLE;
    return;
}
