#pragma once

// ---------------------------------------------------------------------------
// dimension_profile.hpp — per-dimension environment + gameplay flags.
//
// One row per DimensionId slot. Everything that differs between the overworld
// and the nether keys off this table instead of scattering `dim == DIM_NETHER`
// checks: skylight, the panorama skybox, fog tint/range, ambient light floor,
// water evaporation, bed explosions, natural mob spawning and the music set.
// ---------------------------------------------------------------------------

#include "common/dimension.hpp"

// Music set selectors consumed by audio.cpp (see MUSIC_SET_* there).
#define DIM_MUSIC_OVERWORLD 0
#define DIM_MUSIC_NETHER 1

typedef struct {
    const char *name;
    int has_sky;         // skylight exists: column fills, cascade, sky boundary seeds
    int skybox;          // draw the panorama sky (vs a flat fog-colored void)
    float fog_r, fog_g, fog_b; // fog + void tint when !skybox
    float fog_range;     // distance-fog span in blocks (REPGAME_FOG_RANGE for overworld)
    float min_ambient;   // inherent ambient light floor: max()'d with the
                         // player's Cave Brightness (ImGuiDebugVars::min_ambient)
                         // before it reaches the shaders
    int water_allowed;   // 0: water placement evaporates (nether)
    int beds_explode;    // 1: sleeping attempts detonate the bed
    int natural_spawns;  // chunk natural-spawn rolls + night spawner enabled
    int mob_cap;         // despawnable hostiles the night spawner keeps alive
    int music_set;       // DIM_MUSIC_*
} DimensionProfile;

static inline const DimensionProfile *dimension_profile( const DimensionId dim ) {
    static const DimensionProfile profiles[ DIM_COUNT ] = {
        // name          sky skybox  fog (r,g,b)            fog_range ambient water beds spawns cap music
        { "overworld", 1, 1, 0.0f, 0.0f, 0.0f, 96.0f, 0.0f, 1, 0, 1, 12, DIM_MUSIC_OVERWORLD },
        { "nether", 0, 0, 0.20f, 0.03f, 0.02f, 48.0f, 0.20f, 0, 1, 1, 24, DIM_MUSIC_NETHER },
        { "dim2", 0, 0, 0.0f, 0.0f, 0.0f, 96.0f, 0.0f, 0, 0, 0, 12, DIM_MUSIC_OVERWORLD },
        { "dim3", 0, 0, 0.0f, 0.0f, 0.0f, 96.0f, 0.0f, 0, 0, 0, 12, DIM_MUSIC_OVERWORLD },
    };
    return &profiles[ dim >= 0 && dim < DIM_COUNT ? dim : DIM_OVERWORLD ];
}

static inline int dimension_has_sky( const DimensionId dim ) {
    return dimension_profile( dim )->has_sky;
}
