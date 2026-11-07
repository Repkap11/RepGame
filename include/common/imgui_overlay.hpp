#pragma once

typedef struct {
    bool show_demo_window;
    // Runtime visibility of the debug overlay, toggled by the 'g' key.
    // SHOW_IMGUI is the initial value.
    bool shown;
} ImGuiOverlay;

#include "RepGame.hpp"

// Absolute light floor (ImGuiDebugVars::min_ambient): default, pause-menu
// step size, and max.
#define MIN_AMBIENT_DEFAULT 0.05f
#define MIN_AMBIENT_STEP 0.05f
#define MIN_AMBIENT_MAX 1.0f

struct ImGuiDebugVars {
    glm::vec3 player_pos;
    // Day/night clock (see light.hpp) — displayed in the overlay.
    long world_time;
    float daylight;
    // Minimum light levels (0-1): min_ambient is an absolute floor applied in
    // the chunk/object shaders so pitch-black caves stay faintly visible;
    // night_ambient is a floor on the daylight factor, i.e. "moonlight".
    float min_ambient = MIN_AMBIENT_DEFAULT;
    float night_ambient = 0.20f;
    // Chunk-loading and lighting queue depths, refreshed once per frame by
    // RepGame::draw (see LoaderDebugStats in chunk_loader.hpp).
    LoaderDebugStats loader;
    // Live instanced-particle counts, refreshed once per frame.
    int debris_particles;
    int primed_tnt;
    int falling_blocks;
    int item_entities;
    // In-hand flat item pose (camera space), tuned live from the ImGui
    // overlay: translate, euler rotation in degrees (applied yaw about Y,
    // then pitch about X, then roll about Z), and uniform scale.
    glm::vec3 held_pos = glm::vec3( -0.35f, -0.16f, -0.46f );
    glm::vec3 held_rot = glm::vec3( 0.0f, 250.0f, 0.0f ); // pitch, yaw, roll
    float held_scale = 0.2f;
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
void imgui_overlay_draw( ImGuiOverlay *ui_overlay, Input &input, RepGameState &state );
void imgui_overlay_cleanup( ImGuiOverlay *ui_overlay );
ImGuiDebugVars &imgui_overlay_get_imgui_debug_vars( );
// Pause-menu stepper for min_ambient: snaps current to the MIN_AMBIENT_STEP
// grid, moves dir steps, and clamps to [0, MIN_AMBIENT_MAX].
float min_ambient_step( float current, int dir );