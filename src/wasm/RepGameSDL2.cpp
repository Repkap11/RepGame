#include <math.h>
#include <time.h>
#include <unistd.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include "common/RepGame.hpp"
#include "common/utils/env_config.hpp"
#include "common/audio.hpp"
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
    // Touch-to-mouse emulation follows touch_ui: with the C++ touch
    // controls on, SDL's synthetic mouse events are disabled so they
    // can't double-drive clicks; with them off, a finger just behaves
    // as a mouse. The hint is read through a callback, so setting it
    // live flips the emulation mid-session.
    static int touch_mouse_events_on = -1;
    const int want_mouse_events = input.touch_ui ? 0 : 1;
    if ( touch_mouse_events_on != want_mouse_events ) {
        touch_mouse_events_on = want_mouse_events;
        SDL_SetHint( SDL_HINT_TOUCH_MOUSE_EVENTS, want_mouse_events ? "1" : "0" );
    }
    // Capture mode: ignore user input entirely (the camera is scripted), but
    // still honor window resize/close — same rule as the desktop loop.
    const bool capture = RepGame::capture_mode( );
    // Gate look motion on the browser's ACTUAL pointer lock state. SDL2's
    // emscripten backend can't warp the cursor and browser pointer lock is
    // granted asynchronously, so queued pre-lock absolute-position events
    // (and Chrome's spurious first movementX/Y after lock) would produce a
    // huge xrel/yrel camera snap. Drop all mouse motion in the lock-acquire
    // batch; if that batch had none, drop the first motion after it instead.
    static bool was_pointer_locked = false;
    static bool discard_locked_motion = false;
    EmscriptenPointerlockChangeEvent pointerlock_status;
    emscripten_get_pointerlock_status( &pointerlock_status );
    const bool pointer_locked = pointerlock_status.isActive;
    if ( pointer_locked && !was_pointer_locked ) {
        discard_locked_motion = true;
    }
    was_pointer_locked = pointer_locked;
    bool discarded_this_batch = false;
    while ( SDL_PollEvent( &event ) ) {
        bool handledMouse = false;
        bool handledKeyboard = false;
#if SUPPORTS_IMGUI_OVERLAY
        if ( !capture ) {
            imgui_overlay_handle_sdl2_event( &globalGameState->ui.imgui, &event, &handledMouse, &handledKeyboard );
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
            case SDL_TEXTINPUT:
                input.textInput( event.text.text );
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
                if ( input.touch_ui && event.button.which == SDL_TOUCH_MOUSEID ) {
                    // Touch-synthesized mouse event — the C++ touch
                    // controls already handled the finger; drop it
                    // (the hint above should prevent these anyway).
                    break;
                }
                input.mouseInput( event.button.button, event.type == SDL_MOUSEBUTTONUP );
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
                // Real mouse motion is applied only while the browser
                // reports the pointer locked (and not in the discard
                // batch — see above). Touch motion goes through the
                // SDL_FINGER* path instead.
                if ( input.touch_ui && event.motion.which == SDL_TOUCH_MOUSEID ) {
                    break;
                }
                if ( pointer_locked ) {
                    if ( discard_locked_motion ) {
                        discarded_this_batch = true;
                    } else {
                        input.lookMove( event.motion.xrel, event.motion.yrel );
                    }
                }
                input.mousePosition( event.motion.x, event.motion.y );
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP:
                if ( capture ) {
                    break;
                }
                // tfinger x/y are normalized over the canvas's CSS size —
                // scale into window pixels (the same space
                // mouse.absPosition uses; screen.* is what changeSize
                // received, i.e. CSS px, no devicePixelRatio factor).
                // SDL2's emscripten backend maps the browser's touchcancel
                // to FINGERUP (a cancel without a tap).
                repgame.touch_event( event.type == SDL_FINGERDOWN     ? TOUCH_DOWN
                                     : event.type == SDL_FINGERMOTION ? TOUCH_MOVE
                                     : event.type == SDL_FINGERUP     ? TOUCH_UP
                                                                      : TOUCH_CANCEL,
                                     event.tfinger.fingerId,
                                     event.tfinger.x * static_cast<float>( globalGameState->screen.width ),
                                     event.tfinger.y * static_cast<float>( globalGameState->screen.height ) );
                break;
            default:
                break;
        }
    }
    if ( discard_locked_motion && discarded_this_batch ) {
        discard_locked_motion = false;
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
int repgame_sdl2_main( const char *world_path, const char *host, const bool connect_multi, const int test_argc, const char *const *test_argv ) {
    if ( test_argv != nullptr ) {
        return rep_tests_start( test_argc, test_argv );
    }
    if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_AUDIO ) != 0 ) { /* Initialize SDL's Video+Audio subsystems */
        pr_debug( "Unable to initialize SDL" );                 /* Or die on error */
        repgame_fatal( );
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
    const bool msaa = env_bool( "REPGAME_MSAA", true );
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
        repgame_fatal( );
    }
    sdl_context = SDL_GL_CreateContext( sdl_window );
    if ( !sdl_context ) {
        pr_debug( "Creating the SDL context failed" );
        repgame_fatal( );
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
        repgame_fatal( ); // or handle the error in a nicer way
    }
    ignoreErrors( );
    RepGame *repgamePrt = new RepGame( );
    RepGame &repgame = *repgamePrt;
    globalGameState = repgame.init( world_path, connect_multi, host, supportsAnisotropicFiltering );
#if SUPPORTS_IMGUI_OVERLAY
    imgui_overlay_attach_to_window( &globalGameState->ui.imgui, sdl_window, sdl_context );
#endif
    // Same as the native init path: seed screen size/proj explicitly so the
    // first frame is correct even if no SDL_WINDOWEVENT_RESIZED arrives
    // (headless browsers may never fire one).
    repgame.changeSize( default_width, default_height );
    // Enable the on-screen touch controls on coarse-pointer (mobile)
    // devices; ?touch=0|1 overrides (touch=1 lets a desktop browser test
    // the touch UI, touch=0 keeps mouse controls on a touch laptop).
    globalGameState->input.touch_ui = EM_ASM_INT( {
        var t = new URLSearchParams( location.search ).get( 'touch' );
        if ( t === '0' ) {
            return 0;
        }
        if ( t === '1' ) {
            return 1;
        }
        return window.matchMedia && matchMedia( '(pointer: coarse)' ).matches ? 1 : 0;
    } ) != 0;
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

// Called from JS (index.js) when ESC breaks pointer lock during gameplay —
// the browser consumes that keypress, so SDL never sees it. Opens the pause
// menu (idempotent, unlike the ESC toggle in Input::keysInput).
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_request_menu( ) {
    if ( globalGameState && !globalGameState->input.menu_open && !globalGameState->input.inventory_open ) {
        globalGameState->input.onMenuClicked( );
    }
}

// Called from JS on the mobile history-back gesture while the touch UI is
// active: routes through Input::backPressed (abandons an open text capture
// or search box, otherwise acts like Menu/Escape). Exiting stays on the
// pause menu's Exit Game button — back never kills the session.
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_back( ) {
    if ( globalGameState ) {
        globalGameState->input.backPressed( );
    }
}

// Whether the touch controls are active. index.js polls this before any
// pointer-lock attempt and to pick the back-button behaviour.
extern "C" EMSCRIPTEN_KEEPALIVE int repgame_wasm_touch_ui( ) {
    return globalGameState && globalGameState->input.touch_ui ? 1 : 0;
}

// Whether the soft keyboard should be up (index.js focuses its hidden
// input on the next touchend while this is true, blurs it when it drops).
extern "C" EMSCRIPTEN_KEEPALIVE int repgame_wasm_wants_text( ) {
    if ( !globalGameState ) {
        return 0;
    }
    const Input &in = globalGameState->input;
    return in.text_input_active || in.search_focused || globalGameState->ui.chat.open ? 1 : 0;
}

// Soft-keyboard text: feeds the modal text capture (sign edit / chat)
// through Input::textInput, or the creative search box the way
// keysInput's printable-ASCII path does.
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_text( const char *utf8 ) {
    if ( !globalGameState || utf8 == nullptr ) {
        return;
    }
    Input &in = globalGameState->input;
    if ( in.text_input_active ) {
        in.textInput( utf8 );
    } else if ( in.search_focused ) {
        for ( const unsigned char *p = reinterpret_cast<const unsigned char *>( utf8 );
              *p >= 32 && *p <= 126 && in.search_chars_len < ( int )sizeof( in.search_chars ); p++ ) {
            in.search_chars[ in.search_chars_len++ ] = static_cast<char>( *p );
        }
    }
}

// Soft-keyboard editing keys: k=0 backspace, k=1 enter — whichever text
// capture owns the keyboard drains the matching counter.
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_text_key( int k ) {
    if ( !globalGameState ) {
        return;
    }
    Input &in = globalGameState->input;
    if ( k == 0 ) {
        if ( in.text_input_active ) {
            in.text_backspaces++;
        } else if ( in.search_focused && in.search_backspaces < ( int )sizeof( in.search_chars ) ) {
            in.search_backspaces++;
        }
    } else if ( k == 1 ) {
        if ( in.text_input_active ) {
            in.text_enters++;
        } else if ( in.search_focused ) {
            in.search_close = true;
        }
    }
}

// Player position getters for the touch-capture harness (it can't read
// the game state directly; joystick drags show up here).
extern "C" EMSCRIPTEN_KEEPALIVE double repgame_wasm_player_x( ) {
    return globalGameState ? globalGameState->camera.pos.x : 0.0;
}
extern "C" EMSCRIPTEN_KEEPALIVE double repgame_wasm_player_z( ) {
    return globalGameState ? globalGameState->camera.pos.z : 0.0;
}

// Touch-control rects for the touch-capture harness, in window pixels
// (top-left origin, y down). index 0..12 follows UiLayoutRects order:
// joystick, jump, sneak, menu, chat, break, pick, place, inventory,
// inv_close, inv_mode, page_prev, page_next; component 0..3 -> x,y,w,h.
// (Scalar-returning because HEAPF32 isn't an exported runtime method.)
extern "C" EMSCRIPTEN_KEEPALIVE float repgame_wasm_touch_rect( int index, int component ) {
    if ( !globalGameState || index < 0 || index >= 13 || component < 0 || component >= 4 ) {
        return 0.0f;
    }
    const UiLayoutRects &r = globalGameState->ui.layout.rects( );
    const UiRect rects[ 13 ] = { r.touch_joystick, r.touch_jump, r.touch_sneak, r.touch_menu, r.touch_chat,
                                 r.touch_break, r.touch_pick, r.touch_place, r.touch_inventory,
                                 r.touch_inv_close, r.touch_inv_mode, r.touch_page_prev, r.touch_page_next };
    const float w = globalGameState->screen.width;
    const float h = globalGameState->screen.height;
    switch ( component ) {
        case 0:
            return rects[ index ].left + w * 0.5f;
        case 1:
            return h * 0.5f - rects[ index ].top( );
        case 2:
            return rects[ index ].width;
        default:
            return rects[ index ].height;
    }
}

static bool gate_prev_menu_open = false, gate_prev_inventory_open = false;
static bool pending_menu_close = false, pending_inventory_close = false;
static double pending_since_ms = 0;
// Id of the latest gate lock request; results come back per-request via
// repgame_wasm_pointer_lock_result so a stale failure can't clobber a newer
// request.
static int lock_request_id = 0;

static void gate_request_lock( ) {
    ++lock_request_id;
    pending_since_ms = emscripten_get_now( );
    EM_ASM( { repgame_gate_request_pointer_lock( $0 ); }, lock_request_id );
}

// Pointer lock gate: Chrome refuses re-lock within ~1s of the last release.
// If the pause menu or inventory closed this tick but the browser hasn't
// granted the lock yet, revert the close so the UI never disappears without
// the lock behind it; apply it for real once the lock lands.
static void gate_pointer_lock_transition( RepGame &repgame ) {
    Input &in = repgame.getInputState( );
    // Touch devices never get pointer lock; capture mode is scripted.
    // The touch UI draws its own controls, so it bypasses the gate too —
    // no pointer lock is requested when a tap closes a UI screen.
    if ( RepGame::capture_mode( ) || in.touch_ui ||
         EM_ASM_INT( { return typeof pointerLockSupported !== 'undefined' && pointerLockSupported ? 1 : 0; } ) == 0 ) {
        gate_prev_menu_open = in.menu_open;
        gate_prev_inventory_open = in.inventory_open;
        return;
    }
    EmscriptenPointerlockChangeEvent pointerlock_status;
    emscripten_get_pointerlock_status( &pointerlock_status );
    if ( pointerlock_status.isActive ) {
        if ( pending_menu_close || pending_inventory_close ) {
            // Lock arrived — apply the close we held back.
            if ( pending_menu_close ) {
                in.menu_open = false;
            }
            if ( pending_inventory_close ) {
                in.inventory_open = false;
            }
            pending_menu_close = false;
            pending_inventory_close = false;
        }
    } else if ( pending_menu_close || pending_inventory_close ) {
        // Still waiting. If the user performed another close action (Resume
        // click, E/Escape), re-assert the UI and issue a fresh request so
        // that action's user activation isn't wasted; the new id supersedes
        // the old request.
        if ( !in.menu_open && !in.inventory_open ) {
            in.menu_open = pending_menu_close;
            in.inventory_open = pending_inventory_close;
            gate_request_lock( );
        }
        if ( emscripten_get_now( ) - pending_since_ms > 1500 ) {
            pending_menu_close = false;
            pending_inventory_close = false;
        }
    } else if ( ( gate_prev_menu_open && !in.menu_open ) || ( gate_prev_inventory_open && !in.inventory_open ) ) {
        if ( !in.menu_open && !in.inventory_open ) {
            // A close this tick while unlocked means the game is about to
            // want the lock — hold it until the browser grants it.
            pending_menu_close = gate_prev_menu_open;
            pending_inventory_close = gate_prev_inventory_open;
            in.menu_open = gate_prev_menu_open;
            in.inventory_open = gate_prev_inventory_open;
            // Request the lock ourselves (SDL2's own request is unhandled);
            // the result arrives via repgame_wasm_pointer_lock_result.
            gate_request_lock( );
        }
    }
    gate_prev_menu_open = in.menu_open;
    gate_prev_inventory_open = in.inventory_open;
}

// Called from JS (index.js) with the outcome of a specific gate lock
// request. Only the latest request's failure counts — a stale rejection
// racing a newer request is ignored — and success is applied by the
// locked-branch polling in the gate.
extern "C" EMSCRIPTEN_KEEPALIVE void repgame_wasm_pointer_lock_result( int id, int ok ) {
    if ( id != lock_request_id || !( pending_menu_close || pending_inventory_close ) || ok ) {
        return;
    }
    pending_menu_close = false;
    pending_inventory_close = false;
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
        // Clear smoothed look deltas so stale motion can't carry across the
        // transition (the browser can't warp the cursor — SDL_WarpMouseInWindow
        // is a no-op here that would only corrupt SDL's last position).
        input.mouse.smoothed_dx = 0.0f;
        input.mouse.smoothed_dy = 0.0f;
#if ALLOW_GRAB_MOUSE
        // Skip the relative-mode request entirely under the touch UI:
        // SDL2's emscripten backend implements it as a pointer-lock
        // request, which the touch controls never want.
        if ( !input.touch_ui ) {
            SDL_SetRelativeMouseMode( should_lock_pointer ? SDL_TRUE : SDL_FALSE );
        }
#endif
        is_locking_pointer = should_lock_pointer;
    }
    // SDL2: text events only fire while SDL text input is active; sync it
    // to the sign editor's capture flag (no window arg on SDL2).
    static bool text_input_started = false;
    const bool text_active = repgame.getInputState( ).text_input_active;
    if ( text_active != text_input_started ) {
        if ( text_active ) {
            SDL_StartTextInput( );
        } else {
            SDL_StopTextInput( );
        }
        text_input_started = text_active;
    }
}

static inline long long now_us( ) {
    return static_cast<long long>( emscripten_get_now( ) * 1000.0 );
}

void main_loop_wasm( void *arg ) {
    repgame_main_probe.frame++;
    RepGame &repgame = *static_cast<RepGame *>( arg );
    if ( repgame.shouldExit( ) ) {
        // Stop audio before the blocking save below: SDL2's ScriptProcessorNode
        // is refilled from this thread, and while cleanup() stalls it the
        // browser loops the last buffer as a harsh constant tone.
        audio_cleanup( );
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
        // Revert key-driven menu/inventory closes while the browser still
        // hasn't granted pointer lock, before SDL/tick see the want-lock.
        gate_pointer_lock_transition( repgame );
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

    // Catch tick-driven closes (e.g. the Resume button) so the unlocked
    // frame is never drawn.
    gate_pointer_lock_transition( repgame );

    repgame_main_probe.phase = PHASE_CLEAR;
    repgame.clear( );
    repgame_main_probe.phase = PHASE_DRAW;
    repgame.draw( alpha );
    repgame_main_probe.phase = PHASE_IDLE;
    return;
}
