#pragma once

typedef struct {
    bool show_demo_window;
    // Runtime visibility of the debug overlay, toggled by the 'g' key.
    // SHOW_IMGUI is the initial value.
    bool shown;
} ImGuiOverlay;

#include "RepGame.hpp"

struct ImGuiDebugVars {
    glm::vec3 player_pos;
    // Day/night clock (see light.hpp) — displayed in the overlay.
    long world_time;
    float daylight;
    // Minimum light levels (0-1): min_ambient is an absolute floor applied in
    // the chunk/object shaders so pitch-black caves stay faintly visible;
    // night_ambient is a floor on the daylight factor, i.e. "moonlight".
    float min_ambient = 0.05f;
    float night_ambient = 0.08f;
    // Footprint-mipmapped light tuning (chunk_fragment light_lod): bias shifts
    // the LOD higher (blurs the per-block light pattern at closer range);
    // far_ao adds a small deliberate bias toward the solid-inclusive average
    // (0 = air cells only). It stays small on purpose — the 0.1 default is
    // intentional: a surface-straddling texel's air fraction reflects where
    // the texel landed as much as real occlusion, so a large blend darkens
    // even flat unoccluded ground.
    float light_lod_bias = 3.0f;
    float light_far_ao = 0.1f;
    // Cutout-foliage distance smoothing (chunk_fragment hashed alpha test):
    // texel lod range over which the hashed cutoff fades to zero, converging
    // distant canopies to solid leaf green instead of salt-and-pepper
    // speckle. lod = log2(texels per pixel): 0 is 1:1, 3 is 8 texels/px.
    float cutout_fade_start = 0.3f;
    float cutout_fade_end = 1.2f;
    // Chunk-loading and lighting queue depths, refreshed once per frame by
    // RepGame::draw (see LoaderDebugStats in chunk_loader.hpp).
    LoaderDebugStats loader;
    // Live instanced-particle counts, refreshed once per frame.
    int debris_particles;
    int primed_tnt;
};

#if ( defined( REPGAME_WINDOWS ) || defined( REPGAME_LINUX ) || defined( REPGAME_WASM ) )
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