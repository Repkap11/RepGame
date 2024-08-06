#ifndef HEADER_IMGUI_OVERLAY_H
#define HEADER_IMGUI_OVERLAY_H

typedef struct {
    bool show_demo_window;
} ImGuiOverlay;

typedef struct {
    struct {
        float x, y;
    } corner1;
    struct {
        float x, y;
    } corner3;
} ImGuiDebugVars;

#include "RepGame.hpp"

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
void imgui_overlay_draw( ImGuiOverlay *ui_overlay, InputState *input );
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay );
ImGuiDebugVars &imgui_overlay_get_imgui_debug_vars( );

#endif