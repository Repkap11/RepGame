#pragma once

// Mob stress world (REPGAME_MOB_WORLD): a block-world-style debug mode — no
// terrain, disk, network or spawner — whose layout is a walled sandstone
// arena filled with a configurable mob population, for measuring how mob
// tick/render cost scales. Spec:
//
//   REPGAME_MOB_WORLD="<count>[,ai=0|1][,max=<n>][,<type>...]"
//
// count    initial population (also the top-up target)
// ai=0     start frozen (no_ai: render + animation only, no AI/physics)
// max=<n>  arena capacity (default count) — the ImGui target slider's ceiling
// <type>   mob_types[] names cycled round-robin (default: every type)
//
// See docs/mobs.md "Mob stress world".

#include <vector>

#include <glm/glm.hpp>

#include "common/block_types.hpp"
#include "common/mob_types.hpp"

// Blocks between neighbouring grid cells (one mob per cell at spawn).
#define MOB_STRESS_SPACING 3
// Sanity ceiling on count/max: keeps the arena inside a few chunks' worth.
#define MOB_STRESS_MAX 20000

struct MobStressSpec {
    int count = 0; // 0 = mode off
    int capacity = 0;
    bool ai = true;
    std::vector<MobType> types; // round-robin order; never empty when count > 0
    int side = 0;               // grid cells per row (ceil(sqrt(capacity)))
    int origin = 0;             // interior spans [origin, origin + side*SPACING) on x and z
    bool enabled( ) const { return count > 0; }
    int extent( ) const { return side * MOB_STRESS_SPACING; }
};

// Parses a spec string (logs and skips bad tokens; null/empty/count<=0 is
// the disabled spec).
MobStressSpec mob_stress_parse( const char *text );
// REPGAME_MOB_WORLD, parsed once.
const MobStressSpec &mob_stress_spec( );

// Feet position (block-centered, y=1) of grid cell `i` (wraps at capacity).
glm::dvec3 mob_stress_cell_pos( const MobStressSpec &spec, int i );
// Arena layout: sandstone at y<=0, a 2-high stone wall ring just outside
// the interior at y=1..2, air elsewhere. O(1) per cell.
BlockCell mob_stress_arena_cell( const MobStressSpec &spec, int x, int y, int z );
