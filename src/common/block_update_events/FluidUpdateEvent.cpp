#include <queue>
#include <unordered_map>

#include "common/block_update_events/FluidUpdateEvent.hpp"
#include "common/RepGame.hpp"
#include "common/fluid.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

static const glm::ivec3 fluid_horiz_dirs[ 4 ] = {
    glm::ivec3( 1, 0, 0 ),  //
    glm::ivec3( -1, 0, 0 ), //
    glm::ivec3( 0, 0, 1 ),  //
    glm::ivec3( 0, 0, -1 ), //
};
static const glm::ivec3 fluid_up( 0, 1, 0 );
static const glm::ivec3 fluid_down( 0, -1, 0 );

static inline bool same_fluid( const BlockState &state, const BlockID fluid ) {
    return fluid_in_cell( state ) == fluid;
}

// Whether `fluid` may write `level` into the cell: a same-fluid cell only
// when the write improves on its level; otherwise the cell must be empty of
// either fluid and be air-like (can_be_placed_in) or a break-in-liquid
// block that isn't waterloggable. Waterlogged/waterloggable cells, the
// other fluid, and unloaded cells are walls.
static bool fluid_accepts( const BlockID fluid, const BlockState &state, const int level ) {
    if ( state.id == LAST_BLOCK_ID ) {
        return false;
    }
    const BlockID in_cell = fluid_in_cell( state );
    if ( in_cell == fluid ) {
        return fluid_level( state ) > level;
    }
    if ( in_cell != LAST_BLOCK_ID ) {
        return false;
    }
    const Block *def = block_definition_get_definition( state.id );
    return def->can_be_placed_in || ( def->breaks_in_liquid && !def->waterloggable && !state.waterlogged );
}

// A cell the slope search may cross: the fluid could write into it, or it
// already holds the same fluid at any level (the flow passes through
// itself).
static bool fluid_passable( const BlockID fluid, const BlockState &state ) {
    return same_fluid( state, fluid ) || fluid_accepts( fluid, state, FLUID_MAX_LEVEL );
}

// The below-cell spill test: the cell doesn't hold this fluid and would
// accept the level-1 falling write.
static bool fluid_fallable( const BlockID fluid, const BlockState &state ) {
    return !same_fluid( state, fluid ) && fluid_accepts( fluid, state, 1 );
}

// Whether the column can keep descending through the below cell — for the
// sideways gate and the slope search's "drop". An empty-or-fillable cell
// (fallable) or flowing same fluid below counts: a live fall stays an open
// drop, so a formed column keeps pulling the flow toward the hole. A
// same-fluid cell at source level is a still pool, not a drop.
static bool fluid_descends( const BlockID fluid, const BlockState &state ) {
    if ( same_fluid( state, fluid ) ) {
        return fluid_level( state ) > 0;
    }
    return fluid_accepts( fluid, state, 1 );
}

// Effective feed level of a horizontal same-fluid neighbor: sources feed at
// 0, and so do falling cells — a cell with the same fluid above it is part
// of a column and carries source strength sideways.
static int fluid_fed_effective( World &world, const glm::ivec3 &pos, const BlockState &state, const BlockID fluid ) {
    const int level = fluid_level( state );
    if ( level == 0 ) {
        return 0;
    }
    return same_fluid( world.get_loaded_block( pos + fluid_up ), fluid ) ? 0 : level;
}

// Lava's instant-petrify test, shared by the scheduler (+1 tick when a
// reaction is already possible) and the event itself: water in any of the 4
// horizontal neighbors or the cell above. Waterlogged counts as water.
static bool lava_touching_water( World &world, const glm::ivec3 &pos ) {
    if ( fluid_in_cell( world.get_loaded_block( pos + fluid_up ) ) == WATER ) {
        return true;
    }
    for ( const glm::ivec3 &dir : fluid_horiz_dirs ) {
        if ( fluid_in_cell( world.get_loaded_block( pos + dir ) ) == WATER ) {
            return true;
        }
    }
    return false;
}

void fluid_schedule_update( RepGameState &state, const glm::ivec3 &pos ) {
    const BlockState cell = state.world.get_loaded_block( pos );
    const BlockID fluid = cell.id == LAST_BLOCK_ID ? LAST_BLOCK_ID : fluid_in_cell( cell );
    if ( fluid == LAST_BLOCK_ID || !state.pending_fluid_updates.insert( pos ).second ) {
        return;
    }
    const Block *def = block_definition_get_definition( fluid );
    const long delay = ( fluid == LAVA && lava_touching_water( state.world, pos ) ) ? 1 : def->flows;
    state.blockUpdateQueue.addBlockUpdate<FluidUpdateEvent>( state.tick_number + delay, pos );
}

int fluid_slope_distance( World &world, const glm::ivec3 &pos, const glm::ivec3 &horiz_dir, const BlockID fluid, const int flow_slope ) {
    std::unordered_map<glm::ivec3, int> dist;
    std::queue<glm::ivec3> pending;
    const glm::ivec3 start = pos + horiz_dir;
    if ( !fluid_passable( fluid, world.get_loaded_block( start ) ) ) {
        return flow_slope + 1;
    }
    // Seeding pos keeps the ring-shaped same-fluid flow from routing the
    // search back through the cell it started from.
    dist.emplace( pos, 0 );
    dist.emplace( start, 1 );
    pending.push( start );
    while ( !pending.empty( ) ) {
        const glm::ivec3 cur = pending.front( );
        pending.pop( );
        const int d = dist[ cur ];
        if ( fluid_descends( fluid, world.get_loaded_block( cur + fluid_down ) ) ) {
            return d;
        }
        if ( d >= flow_slope ) {
            continue;
        }
        for ( const glm::ivec3 &dir : fluid_horiz_dirs ) {
            const glm::ivec3 next = cur + dir;
            if ( dist.count( next ) || !fluid_passable( fluid, world.get_loaded_block( next ) ) ) {
                continue;
            }
            dist.emplace( next, d + 1 );
            pending.push( next );
        }
    }
    return flow_slope + 1;
}

FluidUpdateEvent::FluidUpdateEvent( long tick_number, const glm::ivec3 &pos ) //
    : BlockUpdateEvent( tick_number ), block_pos( pos ) {
    this->name = "FluidUpdateEvent";
}

void FluidUpdateEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {
    repGameState.pending_fluid_updates.erase( this->block_pos );
    World &world = repGameState.world;
    const BlockState cell = world.get_loaded_block( this->block_pos );
    if ( cell.id == LAST_BLOCK_ID ) {
        return;
    }
    const BlockID fluid = fluid_in_cell( cell );
    if ( fluid == LAST_BLOCK_ID ) {
        return;
    }
    const Block *def = block_definition_get_definition( fluid );
    const int level = fluid_level( cell );
    const bool fluid_above = same_fluid( world.get_loaded_block( this->block_pos + fluid_up ), fluid );

    auto queue_write = [ & ]( const glm::ivec3 &pos, const BlockState &state, BreakDrops drops = BreakDrops::None ) {
        blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( this->tick_number, pos, state, false, drops );
    };

    // R — lava touching water petrifies in place: a source becomes
    // obsidian, a flowing cell cobblestone. The scheduler already shortened
    // the delay to 1 tick when this was visible.
    if ( fluid == LAVA && lava_touching_water( world, this->block_pos ) ) {
        const BlockID rock = level == 0 ? OBSIDIAN : COBBLESTONE;
        queue_write( this->block_pos, { rock, BLOCK_ROTATE_0, 0, rock, 0 } );
        return;
    }

    // Level recompute. Sources (level 0, incl. waterlogged) are stable and
    // skip this entirely.
    if ( level > 0 ) {
        int desired;
        if ( fluid_above ) {
            // Part of a falling column: always level 1, fed at strength 0.
            desired = 1;
        } else {
            int best = def->flow_range + 1;
            int plain_sources = 0;
            for ( const glm::ivec3 &dir : fluid_horiz_dirs ) {
                const glm::ivec3 npos = this->block_pos + dir;
                const BlockState nstate = world.get_loaded_block( npos );
                if ( !same_fluid( nstate, fluid ) ) {
                    continue;
                }
                const int fed = fluid_fed_effective( world, npos, nstate, fluid );
                if ( fed < best ) {
                    best = fed;
                }
                if ( fluid_level( nstate ) == 0 && !same_fluid( world.get_loaded_block( npos + fluid_up ), fluid ) ) {
                    plain_sources++;
                }
            }
            const BlockState below = world.get_loaded_block( this->block_pos + fluid_down );
            const bool solid_or_source_below = below.id != LAST_BLOCK_ID &&
                                               ( block_definition_get_definition( below.id )->collides_with_player ||
                                                 ( same_fluid( below, fluid ) && fluid_level( below ) == 0 ) );
            if ( def->forms_sources && best == 0 && plain_sources >= 2 && solid_or_source_below ) {
                // Two adjacent sources over a solid block (or another
                // source) merge into a new source — MC infinite water.
                desired = 0;
            } else if ( best > def->flow_range - 1 ) {
                // Nothing feeds this cell strongly enough: the flow recedes.
                queue_write( this->block_pos, BLOCK_STATE_AIR );
                return;
            } else {
                desired = best + 1;
            }
        }
        if ( desired != level ) {
            // The write's neighbor updates reschedule this cell, so the
            // spread runs against the corrected level next visit.
            queue_write( this->block_pos, fluid_state( fluid, desired ) );
            return;
        }
    }

    // Spread. A falling column spreads at source strength minus the one
    // step it already consumed; everything else at its own level.
    const int eff = fluid_above ? 0 : level;
    // Flow write that breaks a block drops its loot only for water — MC
    // burns drops in lava.
    const BreakDrops flow_drops = fluid == WATER ? BreakDrops::Survival : BreakDrops::None;
    const glm::ivec3 below_pos = this->block_pos + fluid_down;
    const BlockState below = world.get_loaded_block( below_pos );
    bool can_down = fluid_fallable( fluid, below );
    // A lava cell over water turns the water to stone instead of spilling.
    if ( fluid == LAVA && fluid_in_cell( below ) == WATER ) {
        queue_write( below_pos, { STONE, BLOCK_ROTATE_0, 0, STONE, 0 } );
        can_down = false;
    }
    if ( can_down ) {
        queue_write( below_pos, fluid_state( fluid, 1 ), flow_drops );
    }
    // Sideways only once the column can't keep descending — the below cell
    // is a wall or still same fluid (a live fall or a pool). Treating a
    // same-fluid below as "grounded" would let every mid-column cell spill
    // sideways as a level-1 curtain. A source also spreads while it falls.
    const bool grounded = !can_down && !same_fluid( below, fluid );
    if ( ( grounded || level == 0 ) && eff < def->flow_range ) {
        int dist[ 4 ];
        int best = def->flow_slope + 1;
        for ( int i = 0; i < 4; i++ ) {
            dist[ i ] = fluid_slope_distance( world, this->block_pos, fluid_horiz_dirs[ i ], fluid, def->flow_slope );
            if ( dist[ i ] < best ) {
                best = dist[ i ];
            }
        }
        for ( int i = 0; i < 4; i++ ) {
            // No drop in reach: spread all 4 ways. Otherwise only toward
            // the closest drop(s).
            if ( best <= def->flow_slope && dist[ i ] != best ) {
                continue;
            }
            const glm::ivec3 npos = this->block_pos + fluid_horiz_dirs[ i ];
            if ( fluid_accepts( fluid, world.get_loaded_block( npos ), eff + 1 ) ) {
                queue_write( npos, fluid_state( fluid, eff + 1 ), flow_drops );
            }
        }
    }
}
