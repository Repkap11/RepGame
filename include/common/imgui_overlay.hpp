#pragma once

typedef struct {
    bool show_demo_window;
} ImGuiOverlay;

#include "RepGame.hpp"

typedef struct {
    glm::vec3 player_pos;
    // Day/night clock (see light.hpp) — displayed and editable in the overlay.
    long world_time;
    float daylight;
    // Set by the overlay's time slider when the user drags it; RepGame reads
    // this back and applies it to globalGameState.world_time. -1 = untouched.
    long world_time_override = -1;
    // Chunk-loading and lighting queue depths, refreshed once per frame by
    // RepGame::draw (see LoaderDebugStats in chunk_loader.hpp).
    LoaderDebugStats loader;
} ImGuiDebugVars;

#if ( defined( REPGAME_WINDOWS ) || defined( REPGAME_LINUX ) )
#define SUPPORTS_IMGUI_OVERLAY 1
#else
#define SUPPORTS_IMGUI_OVERLAY 0
#endif

void imgui_overlay_init( ImGuiOverlay *imgui_overlay );
#if SUPPORTS_IMGUI_OVERLAY
void imgui_overlay_attach_to_window( ImGuiOverlay *ui_overlay, SDL_Window *window, SDL_GLContext gl_context );
void imgui_overlay_handle_sdl2_event( ImGuiOverlay *ui_overlay, SDL_Event *event, bool *handledMouse, bool *handledKeyboard );
#endif
void imgui_overlay_draw( ImGuiOverlay *ui_overlay, Input &input );
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay );
ImGuiDebugVars &imgui_overlay_get_imgui_debug_vars( );