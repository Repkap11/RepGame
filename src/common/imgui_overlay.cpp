#include "common/imgui_overlay.hpp"
#include "common/audio.hpp"
#include "common/map_gen.hpp"

ImGuiDebugVars debug_vars;

ImGuiDebugVars &imgui_overlay_get_imgui_debug_vars( ) {
    static bool env_parsed = false;
    if ( !env_parsed ) {
        env_parsed = true;
        // Night brightness overrides: REPGAME_NIGHT_AMBIENT is the
        // moonlight floor on the daylight factor (outdoor night),
        // REPGAME_MIN_AMBIENT is the absolute light floor (caves).
        if ( const char *env = getenv( "REPGAME_NIGHT_AMBIENT" ) ) {
            debug_vars.night_ambient = static_cast<float>( atof( env ) );
        }
        if ( const char *env = getenv( "REPGAME_MIN_AMBIENT" ) ) {
            debug_vars.min_ambient = static_cast<float>( atof( env ) );
        }
    }
    return debug_vars;
}

#if SUPPORTS_IMGUI_OVERLAY

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#if defined( REPGAME_WASM )
// The WASM build uses SDL2 (emscripten -s USE_SDL=2); the SDL2 backend has
// the same API shape as the SDL3 one, so alias it behind generic names.
#include "imgui_impl_sdl2.h"
#include <SDL2/SDL.h>
#define ImGui_ImplSDL_InitForOpenGL ImGui_ImplSDL2_InitForOpenGL
#define ImGui_ImplSDL_ProcessEvent ImGui_ImplSDL2_ProcessEvent
#define ImGui_ImplSDL_NewFrame ImGui_ImplSDL2_NewFrame
#define ImGui_ImplSDL_Shutdown ImGui_ImplSDL2_Shutdown
#else
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#if defined( IMGUI_IMPL_OPENGL_ES2 )
#include <SDL3/SDL_opengles2.h>
#elif !defined( IMGUI_IMPL_OPENGL_ES3 )
// On the GLES build GL prototypes come from the platform header
// (RepGameLinux.hpp -> GLES3/gl32.h); SDL_opengl.h is desktop GL and
// clashes with them.
#include <SDL3/SDL_opengl.h>
#endif
#define ImGui_ImplSDL_InitForOpenGL ImGui_ImplSDL3_InitForOpenGL
#define ImGui_ImplSDL_ProcessEvent ImGui_ImplSDL3_ProcessEvent
#define ImGui_ImplSDL_NewFrame ImGui_ImplSDL3_NewFrame
#define ImGui_ImplSDL_Shutdown ImGui_ImplSDL3_Shutdown
#endif

// Compact counts for the HUD (max 4 chars): raw under 10k, scaled above so
// a fixed-width column never overflows and the text stops reflowing.
static void fmt_count( char *buf, size_t n, long long v ) {
    if ( v < 10000 ) {
        snprintf( buf, n, "%lld", v );
    } else if ( v < 1000000 ) {
        snprintf( buf, n, "%lldk", v / 1000 );
    } else if ( v < 1000000000 ) {
        snprintf( buf, n, "%lldM", v / 1000000 );
    } else {
        snprintf( buf, n, "%lldG", v / 1000000000 );
    }
}

void imgui_overlay_init( ImGuiOverlay *imgui_overlay ) {
    imgui_overlay->shown = SHOW_IMGUI;
    // imgui_overlay->show_demo_window = true;
}

void imgui_overlay_attach_to_window( ImGuiOverlay *ui_overlay, SDL_Window *window, SDL_GLContext gl_context ) {
    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO &io = ImGui::GetIO( );
    // Don't persist window layout to imgui.ini — the overlay is fixed and
    // a leftover ini can shrink the window or add a scrollbar on next launch.
    io.IniFilename = nullptr;
    // The overlay is read-only debug text; don't let it capture the mouse or
    // it eats clicks/motion/wheel whenever the cursor is over the HUD.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark( );

    const char *glsl_version = "#version 300 es";

    // Setup Platform/Renderer backends
    ImGui_ImplSDL_InitForOpenGL( window, gl_context );
    ImGui_ImplOpenGL3_Init( glsl_version );

    float IMGUI_UI_SCALE = 1.6f;

    ImGui::GetStyle( ).ScaleAllSizes( IMGUI_UI_SCALE );
    ImFontConfig cfg;
    cfg.SizePixels = 13 * IMGUI_UI_SCALE;
    io.FontDefault = io.Fonts->AddFontDefault( &cfg );
    ; //->DisplayOffset.y = IMGUI_UI_SCALE;
}

void imgui_overlay_handle_sdl2_event( ImGuiOverlay *ui_overlay, SDL_Event *event, bool *handledMouse, bool *handledKeyboard ) {
    ImGui_ImplSDL_ProcessEvent( event );
    ImGuiIO &io = ImGui::GetIO( );
    // ImGuiConfigFlags_NoMouse keeps io.WantCaptureMouse false, but never
    // report mouse capture regardless: the game always owns the mouse.
    *handledMouse = false;
    *handledKeyboard = io.WantCaptureKeyboard && ui_overlay->shown;
}

void imgui_overlay_draw( ImGuiOverlay *imgui_overlay, Input &input ) {
    ImGui_ImplOpenGL3_NewFrame( );
    ImGui_ImplSDL_NewFrame( );
    ImGui::NewFrame( );

    if ( input.toggle_imgui ) {
        input.toggle_imgui = false;
        imgui_overlay->shown = !imgui_overlay->shown;
        pr_debug( "ImGui overlay: %s", imgui_overlay->shown ? "on" : "off" );
    }

    if ( imgui_overlay->shown ) {
        ImGuiIO &io = ImGui::GetIO( );
        if ( false ) {
            ImGui::ShowDemoWindow( &input.inventory_open );
        } else {
            const char *worldDrawQualityNames[] = { "LOW", "MEDIUM", "HIGH", "X_HIGH" };
            const char *qualityName = worldDrawQualityNames[ static_cast<int>( input.worldDrawQuality ) ];

            // Black background, 50% transparent; no title bar, not movable/selectable.
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.0f, 0.0f, 0.0f, 0.5f ) );
            // ImGuiConfigFlags_NoMouse (set at attach) already makes the
            // overlay non-interactive; NoInputs keeps the window itself from
            // reacting if mouse support is ever re-enabled.
            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize;

            ImGui::SetNextWindowPos( ImVec2( 10.0f, 10.0f ) );
            ImGui::Begin( "RepGame", nullptr, windowFlags );
            ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate );
            ImGui::Text( "Player Pos: %9.1f %9.1f %9.1f", debug_vars.player_pos.x, debug_vars.player_pos.y, debug_vars.player_pos.z );
            ImGui::Text( "Render Mode: %s", qualityName );
            ImGui::Text( "Render Distance: %d chunks (%d blocks)  [=/+ and -, 0 resets]", input.render_distance, input.render_distance * CHUNK_SIZE_X );
            const char *video_driver = SDL_GetCurrentVideoDriver( );
            ImGui::Text( "Video Driver: %s", video_driver ? video_driver : "unknown" );
            const char *acceleration;
            if ( LOAD_CHUNKS_SUPPORTS_CUDA && MapGen::supports_cuda( ) ) {
                acceleration = "CUDA";
            } else if ( LOAD_CHUNKS_SUPPORTS_HIP && MapGen::supports_hip( ) ) {
                acceleration = "HIP";
            } else if ( LOAD_CHUNKS_SUPPORTS_WEBGPU && MapGen::supports_webgpu( ) ) {
                acceleration = "WebGPU";
            } else {
                acceleration = "CPU";
            }
            ImGui::Text( "Acceleration: %s", acceleration );
            // Day/night clock (read-only; PageUp/PageDown nudge world_time,
            // which propagates to the server when connected).
            const long time_of_day = debug_vars.world_time % DAY_LENGTH_TICKS;
            const int clock_minutes = static_cast<int>( time_of_day * 24 * 60 / DAY_LENGTH_TICKS );
            ImGui::Text( "Time: %8ld ticks  %02d:%02d (daylight %.2f)  [PgUp/PgDn]", time_of_day, clock_minutes / 60, clock_minutes % 60, debug_vars.daylight );
            const char *song = audio_current_track_title( );
            ImGui::Text( "Music: %s", song ? song : "--" );

            ImGui::Separator( );
            ImGui::Text( "Chunks: loading %4d (queued %4d, results %4d), drawable %4d",
                         debug_vars.loader.chunks_loading, debug_vars.loader.terrain_queued,
                         debug_vars.loader.terrain_results, debug_vars.loader.chunks_drawable );
            // j=queued jobs p=pending drains s=flood seeds c=in-flight claims
            // d=dirty (awaiting GPU upload) x=dropped seeds
            // lag=oldest job age this interval (all jobs / near-player jobs, ms)
            char lj[32], lp[32], ls[32], ld[32], lx[32];
            fmt_count( lj, sizeof lj, debug_vars.loader.light_jobs );
            fmt_count( lp, sizeof lp, debug_vars.loader.light_pending );
            fmt_count( ls, sizeof ls, debug_vars.loader.light_seeds );
            fmt_count( ld, sizeof ld, debug_vars.loader.light_dirty );
            fmt_count( lx, sizeof lx, debug_vars.loader.light_dropped );
            ImGui::Text( "Light: j:%5s p:%5s s:%5s c:%d d:%5s x:%5s stl:%d lag:%4.0f/%4.0fms%s",
                         lj, lp, ls, debug_vars.loader.light_claims, ld, lx,
                         debug_vars.loader.light_stale_count,
                         debug_vars.loader.light_jlag_us / 1000.0, debug_vars.loader.light_njlag_us / 1000.0,
                         debug_vars.loader.light_bfs_active ? " [BFS]" : "" );
            ImGui::Separator( );

            ImGui::Text( "Particles: %d (%d debris, %d primed TNT)",
                         debug_vars.debris_particles + debug_vars.primed_tnt,
                         debug_vars.debris_particles, debug_vars.primed_tnt );

            // debug_vars.corner1.x = round( debug_vars.corner1.x * 20 ) / 20;
            // debug_vars.corner1.y = round( debug_vars.corner1.y * 20 ) / 20;

            // ImGui::Text( "This is some useful text." );               // Display some text (you can use a format strings too)
            // ImGui::Checkbox( "Demo Window", &input.inventory_open ); // Edit bools storing our window open/close state
            // ImGui::Checkbox( "Another Window", &input.inventory_open );

            // if ( ImGui::Button( "Button" ) ) // Buttons return true when clicked (most widgets return true when edited/activated)
            //     counter++;
            // ImGui::SameLine( );
            // ImGui::Text( "counter = %d", counter );

            ImGui::End( );
            ImGui::PopStyleColor( );
        }
    }
    ImGui::Render( );
    ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData( ) );
}

void imgui_overlay_cleanup( ImGuiOverlay *imgui_overlay ) {
    ImGui_ImplOpenGL3_Shutdown( );
    ImGui_ImplSDL_Shutdown( );
    ImGui::DestroyContext( );
}
#else
void imgui_overlay_init( ImGuiOverlay *imgui_overlay ) {
}
void imgui_overlay_draw( ImGuiOverlay *ui_overlay, Input &input ) {
}
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay ) {
}
#endif