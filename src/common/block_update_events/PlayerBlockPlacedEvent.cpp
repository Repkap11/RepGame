#include <stdlib.h>

#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/schedule.hpp"
#include "common/block_update_events/PistonEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/break_drops.hpp"

static float frand( ) {
    return static_cast<float>( rand( ) ) / static_cast<float>( RAND_MAX );
}

PlayerBlockPlacedEvent::PlayerBlockPlacedEvent( long tick_number, const glm::ivec3 &offset, BlockState blockState, bool state_update, BreakDrops drops,
                                                BreakDebris debris ) //
    : BlockUpdateEvent( tick_number ), block_pos( offset ), blockState( blockState ), state_update( state_update ), drops( drops ), debris( debris ) {
    this->name = "PlayerBlockPlacedEvent";
}

void queue_neighbor_block_updates( RepGameState &state, const glm::ivec3 &pos ) {
    World &world = state.world;
    static const glm::ivec3 neighbor_offsets[ 6 ] = {
        glm::ivec3( 0, 1, 0 ),  //
        glm::ivec3( 0, -1, 0 ), //
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    for ( const glm::ivec3 &offset : neighbor_offsets ) {
        schedule_block_update<BlockNextToChangeEvent>( state, 0, pos, offset );
    }

    // Also notify the dust cells diagonally away: dust at pos+horiz+up or
    // pos+horiz+down may feed on pos or render a connection toward it. Each
    // diagonal edge is gated by the corner cell between the two dusts — the
    // cell above pos for the up-and-over edge, the side cell for the
    // down-and-over edge — so only wake a diagonal dust while its corner is
    // open. Missing a wake left dust stuck powered or dark beside glass.
    const BlockState above_state = world.get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    const bool corner_above_open = above_state.id == LAST_BLOCK_ID || !block_definition_get_definition( above_state.id )->is_solid;
    const glm::ivec3 horiz_offsets[ 4 ] = {
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    for ( const glm::ivec3 &horiz : horiz_offsets ) {
        const BlockState side_state = world.get_loaded_block( pos + horiz );
        if ( side_state.id == LAST_BLOCK_ID ) {
            continue;
        }
        const Block *side = block_definition_get_definition( side_state.id );
        if ( corner_above_open ) {
            schedule_block_update<BlockNextToChangeEvent>( state, 0, pos, horiz + glm::ivec3( 0, 1, 0 ) );
        }
        if ( !side->is_solid ) {
            schedule_block_update<BlockNextToChangeEvent>( state, 0, pos, horiz + glm::ivec3( 0, -1, 0 ) );
        }
    }
}

static void repgame_convert_block_impl( RepGameState &state, const glm::ivec3 &pos, const BlockID new_id, const bool natural ) {
    const BlockState current = state.world.get_loaded_block( pos );
    if ( current.id == LAST_BLOCK_ID || current.id == new_id ) {
        return;
    }
    BlockState next = current;
    next.id = new_id;
    next.display_id = new_id;
    next.rotation = BLOCK_ROTATE_0;
    if ( natural ) {
        state.world.set_loaded_block_natural( pos, next );
    } else {
        state.world.set_loaded_block( pos, next );
    }
    // No debris, on the wire or locally: a conversion transforms the cell in
    // place — grazing, tilling or trampling reads as a swap, not a break.
    state.multiplayer.set_block( pos, next, NET_BU_FLAG_NO_DEBRIS );
    queue_neighbor_block_updates( state, pos );
}

void repgame_convert_block( RepGameState &state, const glm::ivec3 &pos, const BlockID new_id ) {
    repgame_convert_block_impl( state, pos, new_id, false );
}

void repgame_convert_block_natural( RepGameState &state, const glm::ivec3 &pos, const BlockID new_id ) {
    repgame_convert_block_impl( state, pos, new_id, true );
}

// Doors place as a lower+upper pair: a lower needs a collidable support
// below and a replaceable cell above for the upper half.
static bool door_lower_can_place( World &world, const glm::ivec3 &pos ) {
    const BlockState below = world.get_loaded_block( pos + glm::ivec3( 0, -1, 0 ) );
    const BlockState upper_current = world.get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    return below.id != LAST_BLOCK_ID && block_definition_get_definition( below.id )->collides_with_player && //
           upper_current.id != LAST_BLOCK_ID && block_definition_get_definition( upper_current.id )->can_be_placed_in;
}

// Tall plants place as a lower+upper pair like doors: the lower needs its
// normal ground support (checked ignoring the not-yet-written partner) and
// the cell above must be replaceable for the upper half.
static bool tall_plant_lower_can_place( World &world, const glm::ivec3 &pos, const BlockState &lower_state ) {
    const BlockState upper_current = world.get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    return upper_current.id != LAST_BLOCK_ID && block_definition_get_definition( upper_current.id )->can_be_placed_in && //
           block_supports_present( world, pos, lower_state );
}

// Beds place as a foot+head pair like tall plants, but horizontally: the
// head cell sits one block in the placer's facing direction (the foot
// state's yaw). Both cells need their floor support and the head cell must
// be replaceable — the partner isn't written yet, so the generic survive
// check can't apply to either half.
static bool bed_foot_can_place( World &world, const glm::ivec3 &pos, const BlockState &foot_state ) {
    const Block *foot = block_definition_get_definition( foot_state.id );
    const glm::ivec3 head_pos = pos + block_bed_partner_dir( foot, foot_state.rotation );
    const BlockState head_current = world.get_loaded_block( head_pos );
    if ( head_current.id == LAST_BLOCK_ID || !block_definition_get_definition( head_current.id )->can_be_placed_in ) {
        return false;
    }
    BlockState head_state = foot_state;
    head_state.id = foot->bed_partner;
    head_state.display_id = foot->bed_partner;
    return block_supports_present( world, pos, foot_state ) && block_supports_present( world, head_pos, head_state );
}

bool player_placement_valid( World &world, const glm::ivec3 &pos, const BlockState &blockState ) {
    const BlockState current_block_state = world.get_loaded_block( pos );
    if ( current_block_state.id == LAST_BLOCK_ID ) {
        return false;
    }
    if ( BlockStates_equal( blockState, current_block_state ) ) {
        return false;
    }
    Block *new_block = block_definition_get_definition( blockState.id );
    Block *current_block = block_definition_get_definition( current_block_state.id );
    if ( !current_block->can_be_placed_in && !( new_block->flows != 0 && current_block->breaks_in_liquid ) ) {
        return false;
    }
    if ( new_block->is_door && !new_block->is_door_upper ) {
        return door_lower_can_place( world, pos );
    }
    if ( block_is_tall_plant( new_block ) && !new_block->is_tall_plant_upper ) {
        return tall_plant_lower_can_place( world, pos, blockState );
    }
    if ( new_block->bed_partner != LAST_BLOCK_ID && !new_block->is_bed_head ) {
        return bed_foot_can_place( world, pos, blockState );
    }
    return block_can_survive_at( world, pos, blockState );
}

void PlayerBlockPlacedEvent::performAction( RepGameState &repGameState ) {
    BlockState current_block_state = repGameState.world.get_loaded_block( this->block_pos );
    // A door power update takes only the power: the rotation was computed
    // before the update was queued and a toggle since then would be undone
    // by replaying it. Merging first means an all-equal payload still no-ops.
    if ( this->state_update && this->blockState.id == current_block_state.id && this->blockState.id != LAST_BLOCK_ID &&
         block_definition_get_definition( this->blockState.id )->is_door ) {
        this->blockState.rotation = current_block_state.rotation;
    }
    // State already matches, don't change anything
    if ( BlockStates_equal( blockState, current_block_state ) ) {
        return;
    }
    // A state update only makes sense for the block it was computed on. If the
    // block is gone (e.g. just broken while a stale update was still queued),
    // drop it instead of resurrecting the block.
    if ( this->state_update && this->blockState.id != current_block_state.id ) {
        return;
    }
    Block *new_block = block_definition_get_definition( this->blockState.id );
    Block *current_block = block_definition_get_definition( current_block_state.id );

    // If you're placeing a block, the block it's replaceing must have "can_be_placed_in"
    if ( this->blockState.id != AIR && !current_block->can_be_placed_in ) {
        // Unless that block flows, and the other block can be broken by fluid
        if ( new_block->flows != 0 && current_block->breaks_in_liquid ) {
            // This position already has a block, but the new block is a liquid, and the current block will break in a liquid
        } else if ( this->state_update && this->blockState.id == current_block_state.id ) {
            // This position already has the same type of block, allow other state to change.
        } else {
            // This position already has a block that can't be replaced.
            return;
        }
    }
    // Doors place as a lower+upper pair. The generic survive check can't
    // apply (the partner upper isn't written yet), so a lower gets its own
    // placement test and writes both cells; a lone upper fails survive.
    if ( !this->state_update && this->blockState.id != AIR && new_block->is_door && !new_block->is_door_upper ) {
        World &world = repGameState.world;
        const glm::ivec3 upper_pos = this->block_pos + glm::ivec3( 0, 1, 0 );
        const BlockState upper_current = world.get_loaded_block( upper_pos );
        if ( !door_lower_can_place( world, this->block_pos ) ) {
            return;
        }
        // Hinge right when a same-facing door lower sits on the placer's
        // left — this door is then the pair's right member and the two
        // doors mirror like MC double doors.
        const unsigned char facing = this->blockState.rotation;
        static const glm::ivec3 door_facing_dirs[ 4 ] = {
            glm::ivec3( 0, 0, -1 ), // 0: -z
            glm::ivec3( 1, 0, 0 ),  // 1: +x
            glm::ivec3( 0, 0, 1 ),  // 2: +z
            glm::ivec3( -1, 0, 0 ), // 3: -x
        };
        bool hinge_right = false;
        const BlockState left_state = world.get_loaded_block( this->block_pos + door_facing_dirs[ ( facing + 3 ) & 3 ] );
        if ( left_state.id != LAST_BLOCK_ID ) {
            const Block *left = block_definition_get_definition( left_state.id );
            if ( left->is_door && !left->is_door_upper ) {
                const BlockState left_upper = world.get_loaded_block( this->block_pos + door_facing_dirs[ ( facing + 3 ) & 3 ] + glm::ivec3( 0, 1, 0 ) );
                hinge_right = left_upper.id == left->door_partner && door_facing( left_state.rotation, left_upper.rotation ) == facing;
            }
        }
        BlockState lower_state = this->blockState;
        BlockState upper_state = this->blockState;
        upper_state.id = new_block->door_partner;
        upper_state.display_id = new_block->door_partner;
        door_rotations( facing, false, hinge_right, lower_state.rotation, upper_state.rotation );
        world.set_loaded_block( this->block_pos, lower_state );
        repGameState.multiplayer.set_block( this->block_pos, lower_state );
        repGameState.world.spawn_block_debris( this->block_pos, current_block_state, lower_state.id );
        world.set_loaded_block( upper_pos, upper_state );
        repGameState.multiplayer.set_block( upper_pos, upper_state );
        repGameState.world.spawn_block_debris( upper_pos, upper_current, upper_state.id );
        queue_neighbor_block_updates( repGameState, this->block_pos );
        queue_neighbor_block_updates( repGameState, upper_pos );
        return;
    }
    // Tall plants place as a lower+upper pair, same shape as the door
    // branch minus the door rotation/hinge bookkeeping: the upper cell
    // gets the partner block with a neutral rotation; a lone upper fails
    // the survive check below.
    if ( !this->state_update && this->blockState.id != AIR && block_is_tall_plant( new_block ) && !new_block->is_tall_plant_upper ) {
        World &world = repGameState.world;
        const glm::ivec3 upper_pos = this->block_pos + glm::ivec3( 0, 1, 0 );
        const BlockState upper_current = world.get_loaded_block( upper_pos );
        if ( !tall_plant_lower_can_place( world, this->block_pos, this->blockState ) ) {
            return;
        }
        BlockState upper_state = this->blockState;
        upper_state.id = new_block->tall_plant_partner;
        upper_state.display_id = new_block->tall_plant_partner;
        upper_state.rotation = BLOCK_ROTATE_0;
        world.set_loaded_block( this->block_pos, this->blockState );
        repGameState.multiplayer.set_block( this->block_pos, this->blockState );
        repGameState.world.spawn_block_debris( this->block_pos, current_block_state, this->blockState.id );
        world.set_loaded_block( upper_pos, upper_state );
        repGameState.multiplayer.set_block( upper_pos, upper_state );
        repGameState.world.spawn_block_debris( upper_pos, upper_current, upper_state.id );
        queue_neighbor_block_updates( repGameState, this->block_pos );
        queue_neighbor_block_updates( repGameState, upper_pos );
        return;
    }
    // Beds place as a foot+head pair, same shape as the tall-plant branch
    // but horizontal: the head cell sits one block in the facing direction
    // and shares the foot's yaw; a lone head fails the survive check.
    if ( !this->state_update && this->blockState.id != AIR && new_block->bed_partner != LAST_BLOCK_ID && !new_block->is_bed_head ) {
        World &world = repGameState.world;
        const glm::ivec3 head_pos = this->block_pos + block_bed_partner_dir( new_block, this->blockState.rotation );
        const BlockState head_current = world.get_loaded_block( head_pos );
        if ( !bed_foot_can_place( world, this->block_pos, this->blockState ) ) {
            return;
        }
        BlockState head_state = this->blockState;
        head_state.id = new_block->bed_partner;
        head_state.display_id = new_block->bed_partner;
        world.set_loaded_block( this->block_pos, this->blockState );
        repGameState.multiplayer.set_block( this->block_pos, this->blockState );
        repGameState.world.spawn_block_debris( this->block_pos, current_block_state, this->blockState.id );
        world.set_loaded_block( head_pos, head_state );
        repGameState.multiplayer.set_block( head_pos, head_state );
        repGameState.world.spawn_block_debris( head_pos, head_current, head_state.id );
        queue_neighbor_block_updates( repGameState, this->block_pos );
        queue_neighbor_block_updates( repGameState, head_pos );
        return;
    }
    // Don't place a block that can't survive here (e.g. dust above dust, a
    // torch on a missing attachment): it would sit for a tick and then pop
    // off, spawning break debris for a block that was never really placed.
    if ( !this->state_update && !block_can_survive_at( repGameState.world, this->block_pos, this->blockState ) ) {
        return;
    }
    // A stale survival-kill (non-update AIR write) must not clobber a piston
    // head that is still attached to its base: the kill decision was queued
    // before the head landed in the cell. Heads that lost their base still
    // fail can_survive and are removed as intended.
    if ( this->blockState.id == AIR && !this->state_update && current_block->is_piston_head &&
         block_can_survive_at( repGameState.world, this->block_pos, current_block_state ) ) {
        return;
    }
    // If you're destroying a block, the destroyed block must have can_be_destroyed
    // if (this->blockState.id == AIR && !current_block->can_be_destroyed){
    if ( current_block_state.id == AIR && !1 ) {
        return;
    }

    // Water flowing into a waterloggable block waterlogs it instead of
    // breaking it — nothing was destroyed. BreakDebris::None gives the same
    // verdict for removals that aren't breaks (eating the last slice). The
    // flag carries it to remote clients; debris_spawn gates the local burst.
    const bool debris_spawn = this->debris == BreakDebris::Spawn && !( this->blockState.id == WATER && current_block->waterloggable );
    repGameState.world.set_loaded_block( this->block_pos, this->blockState );
    repGameState.multiplayer.set_block( this->block_pos, this->blockState, debris_spawn ? 0 : NET_BU_FLAG_NO_DEBRIS );
    // Removal writes requested drops (support loss in survival, natural leaf
    // decay in either mode, water popping a break-in-liquid block): the cell
    // held something real the write destroyed. Reading the partner cell live
    // means a two-cell block drops exactly once — the half that goes second
    // sees an empty partner.
    const bool drops_ok = this->drops == BreakDrops::Always || //
                          ( this->drops == BreakDrops::Survival && repGameState.game_mode == GameMode_Survival );
    // A fluid write destroys the cell's block only when it breaks it —
    // fluid_accepts' wall test: a waterloggable or already-watered cell gets
    // waterlogged by block_state_normalize instead, so the host survives.
    const bool fluid_broke = new_block->flows != 0 && current_block->breaks_in_liquid && //
                             !current_block->waterloggable && !current_block_state.waterlogged;
    if ( drops_ok && current_block_state.id != AIR && current_block_state.id != LAST_BLOCK_ID &&
         ( this->blockState.id == AIR || fluid_broke ) ) {
        break_drops( repGameState, this->block_pos, current_block_state );
    }
    if ( debris_spawn ) {
        repGameState.world.spawn_block_debris( this->block_pos, current_block_state, this->blockState.id );
    }

    queue_neighbor_block_updates( repGameState, this->block_pos );

    // A door opens/closes on a change to the combined power of both halves —
    // MC edge semantics: rising opens, falling closes, and a hand toggle in
    // between survives until the next edge. Queued 1 game tick out so the
    // new power state is visible; the event re-reads live state, so stale
    // triggers are harmless.
    if ( this->state_update && new_block->is_door && this->blockState.id == current_block_state.id ) {
        const glm::ivec3 other_pos = this->block_pos + ( new_block->is_door_upper ? glm::ivec3( 0, -1, 0 ) : glm::ivec3( 0, 1, 0 ) );
        const int other_power = repGameState.world.get_loaded_block( other_pos ).current_redstone_power;
        const bool powered_before = current_block_state.current_redstone_power > 0 || other_power > 0;
        const bool powered_after = this->blockState.current_redstone_power > 0 || other_power > 0;
        if ( powered_before != powered_after ) {
            const glm::ivec3 lower_pos = new_block->is_door_upper ? other_pos : this->block_pos;
            schedule_redstone_update<DoorEvent>( repGameState, DOOR_DELAY_GT, lower_pos, DoorEvent::SYNC_POWER );
        }
    }
    // A piston starts its move on the power edge's game tick — the slide
    // itself takes PISTON_MOVE_GT before the blocks land. Runs inline, right
    // after the new power state was written above: scheduling a delay-0
    // event instead would leave the base showing its unpowered model for a
    // drain on retraction. The event re-reads live state, so stale triggers
    // are harmless.
    if ( new_block->is_piston && ( current_block_state.current_redstone_power > 0 ) != ( this->blockState.current_redstone_power > 0 ) ) {
        piston_update( repGameState, this->block_pos );
    }

    // TNT ignites on a rising power edge: tnt_ignite removes the block,
    // spawns the primed (flashing) body with a small hop like MC, and
    // queues the explosion for when its fuse ends.
    if ( new_block->is_tnt && current_block_state.current_redstone_power == 0 && this->blockState.current_redstone_power > 0 ) {
        const glm::vec3 hop( ( frand( ) - 0.5f ) * 0.6f, 1.5f, ( frand( ) - 0.5f ) * 0.6f );
        tnt_ignite( repGameState, glm::vec3( this->block_pos ) + 0.5f, hop, TNT_FUSE_TICKS, new_block->is_inverse_tnt );
    }
}