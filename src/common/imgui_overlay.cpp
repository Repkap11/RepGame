#include <math.h>

#include "common/imgui_overlay.hpp"
#include "common/RepGame.hpp" // RepGameState (imgui_overlay_draw's state)
#include "common/utils/env_config.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/map_gen.hpp"
#include "common/utils/frame_stats.hpp"

// Set to 1 to make the overlay interactive while the cursor is free
// (inventory/menu open) and show the held-pose tuning controls.
#define IMGUI_OVERLAY_LIVE_TUNE 0

ImGuiDebugVars debug_vars;

ImGuiDebugVars &imgui_overlay_get_imgui_debug_vars( ) {
    static bool env_parsed = false;
    if ( !env_parsed ) {
        env_parsed = true;
        // Night brightness overrides: REPGAME_NIGHT_AMBIENT is the
        // moonlight floor on the daylight factor (outdoor night),
        // REPGAME_MIN_AMBIENT is the absolute light floor (caves).
        debug_vars.night_ambient = env_float( "REPGAME_NIGHT_AMBIENT", debug_vars.night_ambient );
        debug_vars.min_ambient = env_float( "REPGAME_MIN_AMBIENT", debug_vars.min_ambient );
    }
    return debug_vars;
}

float min_ambient_step( const float current, const int dir ) {
    // Round to the step grid first so off-grid values (e.g. from
    // REPGAME_MIN_AMBIENT) snap cleanly and repeated clicks don't drift.
    const float steps = roundf( current / MIN_AMBIENT_STEP ) + dir;
    const float next = steps * MIN_AMBIENT_STEP;
    return fminf( fmaxf( next, 0.0f ), MIN_AMBIENT_MAX );
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
}

void imgui_overlay_attach_to_window( ImGuiOverlay *ui_overlay, SDL_Window *window, SDL_GLContext gl_context ) {
    IMGUI_CHECKVERSION( );
    ImGui::CreateContext( );
    ImGuiIO &io = ImGui::GetIO( );
    // Don't persist window layout to imgui.ini — the overlay is fixed and
    // a leftover ini can shrink the window or add a scrollbar on next launch.
    io.IniFilename = nullptr;
    // The overlay is read-only debug text while the game owns the mouse;
    // NoMouse keeps it from capturing clicks/motion/wheel over the HUD.
    // imgui_overlay_draw lifts it per-frame while the cursor is free
    // (inventory/menu open) so the live-tuning controls are usable.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

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
}

void imgui_overlay_handle_sdl2_event( ImGuiOverlay *ui_overlay, SDL_Event *event, bool *handledMouse, bool *handledKeyboard ) {
    ImGui_ImplSDL_ProcessEvent( event );
    ImGuiIO &io = ImGui::GetIO( );
    // While the cursor is free (inventory/menu open) imgui_overlay_draw
    // clears NoMouse and ImGui reports real capture, so ImGui eats clicks on
    // its windows instead of the game/inventory. In gameplay NoMouse keeps
    // WantCaptureMouse false and this stays false.
    *handledMouse = io.WantCaptureMouse && ui_overlay->shown;
    *handledKeyboard = io.WantCaptureKeyboard && ui_overlay->shown;
}

void imgui_overlay_draw( ImGuiOverlay *imgui_overlay, Input &input, RepGameState &state ) {
    ImGui_ImplOpenGL3_NewFrame( );
    ImGui_ImplSDL_NewFrame( );
    ImGui::NewFrame( );

    // Fed every frame (even while hidden) so the window is warm when the
    // overlay is toggled on.
    static FrameTimeStats frame_stats;
    frame_stats.add_frame( ImGui::GetIO( ).DeltaTime );

    if ( input.toggle_imgui ) {
        input.toggle_imgui = false;
        imgui_overlay->shown = !imgui_overlay->shown;
        pr_debug( "ImGui overlay: %s", imgui_overlay->shown ? "on" : "off" );
    }

    // The overlay stays up while the pause menu is open; draw order in
    // draw_ui_pass puts the menu on top.
    if ( imgui_overlay->shown ) {
        ImGuiIO &io = ImGui::GetIO( );
        // The overlay is read-only while the game owns the mouse; with the
        // cursor free (inventory/menu open) make ImGui interactive so the
        // live-tuning controls below are usable (IMGUI_OVERLAY_LIVE_TUNE).
        const bool interactive = IMGUI_OVERLAY_LIVE_TUNE && ( input.inventory_open || input.menu_open );
        if ( interactive ) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
        const char *worldDrawQualityNames[] = { "LOW", "MEDIUM", "HIGH", "X_HIGH" };
        const char *qualityName = worldDrawQualityNames[ static_cast<int>( input.worldDrawQuality ) ];

        // Worst-case probe of the widest line (Light:) — %5s fields max
        // at 5 chars, counters/lag at 4 digits, plus the " [BFS]" suffix.
        // Measured once so variable-width counters never resize the GUI;
        // height stays auto-fit via size.y == 0.
        static const float overlay_width = ImGui::CalcTextSize( "Light: j:88888 p:88888 s:88888 c:8888 d:88888 x:88888 stl:8888 lag:8888.8/8888.8ms" ).x + ImGui::GetStyle( ).WindowPadding.x * 2 + 8;

        // Black background, 50% transparent; no title bar, not movable/selectable.
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.0f, 0.0f, 0.0f, 0.5f ) );
        // ImGuiConfigFlags_NoMouse makes the overlay non-interactive
        // during gameplay; NoInputs is the same guard at the window
        // level. Both lift when the cursor is free (interactive above).
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
        if ( !interactive ) {
            windowFlags |= ImGuiWindowFlags_NoInputs;
        }

        ImGui::SetNextWindowPos( ImVec2( 10.0f, 10.0f ) );
        ImGui::SetNextWindowSize( ImVec2( overlay_width, 0 ) );
        ImGui::Begin( "RepGame", nullptr, windowFlags );
        const FrameTimeStats::Summary &fps = frame_stats.summary( );
        ImGui::Text( "FPS avg:%5.1f  1%%:%5.1f  0.1%%:%5.1f  (%6.2f ms)", fps.avg_fps, fps.low1_fps, fps.low01_fps, fps.avg_ms );
        ImGui::Text( "Player Pos: %9.1f %9.1f %9.1f", debug_vars.player_pos.x, debug_vars.player_pos.y, debug_vars.player_pos.z );
        if ( state.block_selection.selectionInBounds ) {
            const BlockState target = state.world.get_loaded_block( state.block_selection.pos_destroy );
            const char *bname = target.id < LAST_BLOCK_ID ? block_definition_get_name( target.id ) : nullptr;
            ImGui::Text( "Looking At: %s (%d)", bname ? bname : "?", (int)target.id );
        } else {
            ImGui::TextUnformatted( "Looking At: --" );
        }
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
        // Server connection status; empty hostname means no server was
        // requested (single player), so show "off" instead.
        const std::string &server = state.multiplayer.server_hostname( );
        if ( server.empty( ) ) {
            ImGui::TextUnformatted( "Multiplayer: off" );
        } else if ( !state.multiplayer.is_active( ) && !state.multiplayer.is_connecting( ) && state.multiplayer.protocol_mismatch_version( ) >= 0 ) {
            ImGui::Text( "Multiplayer: version mismatch (server v%d, client v%d)",
                         state.multiplayer.protocol_mismatch_version( ), NET_PROTOCOL_VERSION );
        } else {
            const char *mp_status = state.multiplayer.is_active( ) ? "connected" : ( state.multiplayer.is_connecting( ) ? "connecting" : "disconnected" );
            ImGui::Text( "Multiplayer: %s:%d %s", server.c_str( ), state.multiplayer.server_port( ), mp_status );
        }
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
        // Async server-diff remesh: s=jobs submitted p=published d=discarded
        // r=promoted to sync, f=currently in flight.
        ImGui::Text( "Remesh async: s:%lld p:%lld d:%lld r:%lld f:%d",
                     debug_vars.loader.async_submitted, debug_vars.loader.async_published,
                     debug_vars.loader.async_discarded, debug_vars.loader.async_promoted,
                     debug_vars.loader.async_in_flight );
        // j=queued jobs p=pending drains s=flood seeds c=in-flight claims
        // d=dirty (awaiting GPU upload) x=dropped seeds
        // lag=oldest job age this interval (all jobs / near-player jobs, ms)
        char lj[32], lp[32], ls[32], ld[32], lx[32];
        fmt_count( lj, sizeof lj, debug_vars.loader.light_jobs );
        fmt_count( lp, sizeof lp, debug_vars.loader.light_pending );
        fmt_count( ls, sizeof ls, debug_vars.loader.light_seeds );
        fmt_count( ld, sizeof ld, debug_vars.loader.light_dirty );
        fmt_count( lx, sizeof lx, debug_vars.loader.light_dropped );
        ImGui::Text( "Light: j:%5s p:%5s s:%5s c:%d d:%5s x:%5s stl:%d lag:%4.0f/%4.0fms",
                     lj, lp, ls, debug_vars.loader.light_claims, ld, lx,
                     debug_vars.loader.light_stale_count,
                     debug_vars.loader.light_jlag_us / 1000.0, debug_vars.loader.light_njlag_us / 1000.0 );
        ImGui::Separator( );

        ImGui::Text( "Particles: %d (%d debris, %d primed TNT, %d falling blocks)",
                     debug_vars.debris_particles + debug_vars.primed_tnt + debug_vars.falling_blocks,
                     debug_vars.debris_particles, debug_vars.primed_tnt, debug_vars.falling_blocks );
        ImGui::Text( "Mobs: %d (players %d)", static_cast<int>( state.world.mobs.count( ) - state.world.mobs.remote_count( ) ),
                     static_cast<int>( state.world.mobs.remote_count( ) ) );
        ImGui::Text( "Items: %d  Arrows: %d", debug_vars.item_entities, static_cast<int>( state.world.arrows.count( ) ) );

#if IMGUI_OVERLAY_LIVE_TUNE
        ImGui::Separator( );
        // Live tuning for the in-hand poses (see draw_ui_pass); usable
        // only while the cursor is free. Edits log once the mouse button
        // comes up; "Log pose" dumps the current values on demand so a
        // settled pose can be baked back into ImGuiDebugVars defaults.
        bool pose_dirty = false;
        if ( ImGui::CollapsingHeader( "Held Item", ImGuiTreeNodeFlags_DefaultOpen ) ) {
            pose_dirty |= ImGui::DragFloat3( "Position", &debug_vars.held_pos.x, 0.01f );
            pose_dirty |= ImGui::DragFloat( "Yaw", &debug_vars.held_rot.y, 0.5f );
            pose_dirty |= ImGui::DragFloat( "Pitch", &debug_vars.held_rot.x, 0.5f );
            pose_dirty |= ImGui::DragFloat( "Roll", &debug_vars.held_rot.z, 0.5f );
            pose_dirty |= ImGui::DragFloat( "Scale", &debug_vars.held_scale, 0.005f, 0.01f, 2.0f );
            ImGui::SeparatorText( "Bow" );
            pose_dirty |= ImGui::DragFloat3( "Bow Position", &debug_vars.bow_pos.x, 0.01f );
            pose_dirty |= ImGui::DragFloat( "Bow Yaw", &debug_vars.bow_rot.y, 0.5f );
            pose_dirty |= ImGui::DragFloat( "Bow Pitch", &debug_vars.bow_rot.x, 0.5f );
            pose_dirty |= ImGui::DragFloat( "Bow Roll", &debug_vars.bow_rot.z, 0.5f );
            pose_dirty |= ImGui::DragFloat( "Bow Scale", &debug_vars.bow_scale, 0.005f, 0.01f, 2.0f );
            pose_dirty |= ImGui::DragFloat( "Bow Spin", &debug_vars.bow_spin, 0.5f );
            ImGui::SeparatorText( "Held Block" );
            pose_dirty |= ImGui::DragFloat3( "B-Position", &debug_vars.held_block_pos.x, 0.01f );
            pose_dirty |= ImGui::DragFloat( "B-Yaw", &debug_vars.held_block_rot.y, 0.5f );
            pose_dirty |= ImGui::DragFloat( "B-Pitch", &debug_vars.held_block_rot.x, 0.5f );
            pose_dirty |= ImGui::DragFloat( "B-Roll", &debug_vars.held_block_rot.z, 0.5f );
            pose_dirty |= ImGui::DragFloat( "B-Scale", &debug_vars.held_block_scale, 0.005f, 0.01f, 2.0f );
            // Remote avatar grip: model-pixel space on the right-arm bone,
            // so positions/scale run 0-32 not 0-1.
            ImGui::SeparatorText( "Avatar Held" );
            pose_dirty |= ImGui::DragFloat3( "A-Position", &debug_vars.avatar_held_pos.x, 0.25f );
            pose_dirty |= ImGui::DragFloat( "A-Yaw", &debug_vars.avatar_held_rot.y, 0.5f );
            pose_dirty |= ImGui::DragFloat( "A-Pitch", &debug_vars.avatar_held_rot.x, 0.5f );
            pose_dirty |= ImGui::DragFloat( "A-Roll", &debug_vars.avatar_held_rot.z, 0.5f );
            pose_dirty |= ImGui::DragFloat( "A-Scale", &debug_vars.avatar_held_scale, 0.1f, 0.5f, 32.0f );
            ImGui::SeparatorText( "Avatar Held Block" );
            pose_dirty |= ImGui::DragFloat3( "AB-Position", &debug_vars.avatar_held_block_pos.x, 0.25f );
            pose_dirty |= ImGui::DragFloat( "AB-Yaw", &debug_vars.avatar_held_block_rot.y, 0.5f );
            pose_dirty |= ImGui::DragFloat( "AB-Pitch", &debug_vars.avatar_held_block_rot.x, 0.5f );
            pose_dirty |= ImGui::DragFloat( "AB-Roll", &debug_vars.avatar_held_block_rot.z, 0.5f );
            pose_dirty |= ImGui::DragFloat( "AB-Scale", &debug_vars.avatar_held_block_scale, 0.1f, 0.5f, 32.0f );
        }
        static bool pose_pending_log = false;
        pose_pending_log |= pose_dirty;
        if ( ImGui::Button( "Log pose" ) || ( pose_pending_log && ImGui::IsMouseReleased( ImGuiMouseButton_Left ) ) ) {
            pose_pending_log = false;
            pr_debug( "held pose: pos=(%.4f %.4f %.4f) rot=(%.3f %.3f %.3f) scale=%.4f | "
                      "bow pos=(%.4f %.4f %.4f) rot=(%.3f %.3f %.3f) scale=%.4f spin=%.2f | "
                      "avatar pos=(%.4f %.4f %.4f) rot=(%.3f %.3f %.3f) scale=%.4f | "
                      "avatar block pos=(%.4f %.4f %.4f) rot=(%.3f %.3f %.3f) scale=%.4f",
                      debug_vars.held_pos.x, debug_vars.held_pos.y, debug_vars.held_pos.z,
                      debug_vars.held_rot.x, debug_vars.held_rot.y, debug_vars.held_rot.z, debug_vars.held_scale,
                      debug_vars.bow_pos.x, debug_vars.bow_pos.y, debug_vars.bow_pos.z,
                      debug_vars.bow_rot.x, debug_vars.bow_rot.y, debug_vars.bow_rot.z,
                      debug_vars.bow_scale, debug_vars.bow_spin,
                      debug_vars.avatar_held_pos.x, debug_vars.avatar_held_pos.y, debug_vars.avatar_held_pos.z,
                      debug_vars.avatar_held_rot.x, debug_vars.avatar_held_rot.y, debug_vars.avatar_held_rot.z, debug_vars.avatar_held_scale,
                      debug_vars.avatar_held_block_pos.x, debug_vars.avatar_held_block_pos.y, debug_vars.avatar_held_block_pos.z,
                      debug_vars.avatar_held_block_rot.x, debug_vars.avatar_held_block_rot.y, debug_vars.avatar_held_block_rot.z, debug_vars.avatar_held_block_scale );
        }
#endif

        ImGui::End( );
        ImGui::PopStyleColor( );
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
void imgui_overlay_draw( ImGuiOverlay *ui_overlay, Input &input, RepGameState &state ) {
}
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay ) {
}
#endif