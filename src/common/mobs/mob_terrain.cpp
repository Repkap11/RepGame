#include "common/mobs.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <stdlib.h>
#include <unordered_map>

#include "common/constants.hpp"
#include "common/arrows.hpp"
#include "common/audio.hpp"
#include "common/chunk.hpp"
#include "common/heart_particles.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/dimension_profile.hpp"
#include "common/imgui_overlay.hpp"
#include "common/item_entities.hpp"
#include "common/light.hpp"
#include "common/mp_test.hpp"
#include "common/net_entities.hpp"
#include "common/ui_overlay_buffers.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/angles.hpp"
#include "common/world.hpp"

#include "mob_internal.hpp"

namespace mob_terrain {

// True while the block at `pos` collides (used for ground probes and the
// auto-jump wall check).
bool solid_at( World &world, const glm::ivec3 &pos ) {
    const BlockState bs = world.get_loaded_block( pos );
    return bs.id < LAST_BLOCK_ID && block_definition_get_definition( bs.id )->collides_with_player;
}

// True while a mob can stand in the cell: non-colliding and not liquid.
// Water and lava don't collide, so plain !solid_at lets spawns land
// submerged — under an ocean or on the nether lava-sea floor.
bool spawnable_air_at( World &world, const glm::ivec3 &pos ) {
    const BlockState bs = world.get_loaded_block( pos );
    if ( bs.id == WATER || bs.id == LAVA ) {
        return false;
    }
    return bs.id == AIR || ( bs.id < LAST_BLOCK_ID && !block_definition_get_definition( bs.id )->collides_with_player );
}

// Height of the ground surface under the column (bx, bz), scanning from
// `from_y` down; returns the top Y of the first solid block or 0/1 in
// *found. On a slope taller than the scan the hit is inside the hillside,
// reporting the face as "ground" — the wander AI's climb/drop band is what
// rejects those as unreachable.
double ground_top_at( World &world, int bx, int bz, int from_y, int max_drop, bool *found ) {
    for ( int y = from_y; y >= from_y - max_drop; y-- ) {
        if ( solid_at( world, glm::ivec3( bx, y, bz ) ) ) {
            *found = true;
            return y + 1.0;
        }
    }
    *found = false;
    return 0.0;
}

// Hover flight's cell predicate: the water surface also counts — a flyer
// skims the surface instead of dunking into it.
bool hover_solid_at( World &world, int bx, int y, int bz ) {
    const BlockState bs = world.get_loaded_block( glm::ivec3( bx, y, bz ) );
    return bs.id == WATER || ( bs.id < LAST_BLOCK_ID && block_definition_get_definition( bs.id )->collides_with_player );
}

// Hover flight's ground probe: height of the ground surface under the
// column (bx, bz), scanning from `from_y` down.
double hover_ground_at( World &world, int bx, int bz, int from_y, int max_drop, bool *found ) {
    for ( int y = from_y; y >= from_y - max_drop; y-- ) {
        if ( hover_solid_at( world, bx, y, bz ) ) {
            *found = true;
            return y + 1.0;
        }
    }
    *found = false;
    return 0.0;
}

// Hover flight's look-ahead probe: scans from above the head for the first
// solid run, then walks it down to its underside and looks for the floor
// below. An opening that fits the mob — `pass_h` of room between the lower
// floor and the run's underside — is flown *through* (a door or the space
// under a tree canopy), so the lower floor is the ground. A solid wall or
// a gap too small reports the run's top and the mob climbs over it.
double hover_ground_ahead_at( World &world, int bx, int bz, int from_y, int max_drop, double pass_h, bool *found ) {
    const int lo_y = from_y - max_drop;
    for ( int y = from_y; y >= lo_y; y-- ) {
        if ( !hover_solid_at( world, bx, y, bz ) ) {
            continue;
        }
        int under = y;
        while ( under - 1 >= lo_y && hover_solid_at( world, bx, under - 1, bz ) ) {
            under--;
        }
        for ( int y2 = under - 1; y2 >= lo_y; y2-- ) {
            if ( hover_solid_at( world, bx, y2, bz ) ) {
                *found = true;
                return under - ( y2 + 1 ) >= pass_h ? y2 + 1.0 : y + 1.0;
            }
        }
        // The run reaches the bottom of the scan (a real wall): climb it.
        *found = true;
        return y + 1.0;
    }
    *found = false;
    return 0.0;
}

} // namespace mob_terrain
