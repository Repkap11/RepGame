#include <queue>
#include <unordered_map>
#include <vector>

#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/FallingBlockEvent.hpp"
#include "common/block_update_events/LeafDecayEvent.hpp"
#include "common/block_update_events/FluidUpdateEvent.hpp"
#include "common/block_update_events/RepeaterEvent.hpp"
#include "common/rail.hpp"
#include "common/repeater.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset ) : BlockUpdateEvent( tick_number ), block_pos( pos + offset ), affecting_block_pos( pos ) {
    this->name = "BlockNextToChangeEvent";
}

// Returns true if the given dust display_id points toward the given horizontal direction,
// i.e. the dust would weakly power a solid block in that direction.
// dir_from_dust points from the dust toward the block being powered.
// Mapping: x+1=left, x-1=right, z+1=front, z-1=back (matching the connection naming in perform_checks).
// A dot (REDSTONE_WIRE_DOT) points nowhere horizontally — it only powers the block below it.
bool dust_points_toward( BlockID display_id, const glm::ivec3 &dir_from_dust ) {
    if ( dir_from_dust.y != 0 ) {
        return false; // vertical power is handled separately
    }
    bool points_left  = ( dir_from_dust.x == 1 );   // dust connects toward x+1
    bool points_right = ( dir_from_dust.x == -1 );   // dust connects toward x-1
    bool points_front = ( dir_from_dust.z == 1 );    // dust connects toward z+1
    bool points_back  = ( dir_from_dust.z == -1 );    // dust connects toward z-1

    switch ( display_id ) {
        case REDSTONE_WIRE_CROSS:     return true;
        case REDSTONE_WIRE_LINE_1:    return points_left || points_right;
        case REDSTONE_WIRE_LINE_2:    return points_front || points_back;
        case REDSTONE_WIRE_L_Q1: return points_right || points_front;  // x-1, z+1
        case REDSTONE_WIRE_L_Q2: return points_left || points_front;   // x+1, z+1
        case REDSTONE_WIRE_L_Q3: return points_left || points_back;    // x+1, z-1
        case REDSTONE_WIRE_L_Q4: return points_right || points_back;   // x-1, z-1
        case REDSTONE_WIRE_T_B:  return points_left || points_right || points_front;  // missing back
        case REDSTONE_WIRE_T_R:  return points_left || points_front || points_back;   // missing right
        case REDSTONE_WIRE_T_F:  return points_right || points_left || points_back;    // missing front
        case REDSTONE_WIRE_T_L:  return points_right || points_front || points_back;   // missing left
        case REDSTONE_WIRE_DOT:       return false;  // dot powers only the block below
        default:                 return true;   // fallback: power all sides
    }
}

#define REDSTONE_DELAY 1
#define REDSTONE_TORCH_POWER 15

// Returns the strongest power the dust at `pos` can read from adjacent non-dust
// blocks (redstone blocks, torches, hard powered blocks). Soft powered blocks
// (power <= 1) and torches attached to `pos` don't count. Dust neighbors are
// handled by compute_dust_power's graph search instead of being read here.
static int dust_adjacent_source_power( World &world, const glm::ivec3 &pos ) {
    int max = 0;
    static constexpr glm::ivec3 directions[ 6 ] = {
        glm::ivec3( 0, 1, 0 ),  //
        glm::ivec3( 0, -1, 0 ), //
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    for ( const glm::ivec3 &dir : directions ) {
        BlockState neighbor = world.get_loaded_block( pos + dir );
        if ( neighbor.id == LAST_BLOCK_ID ) {
            continue;
        }
        // Skip torches that are attached to this position (torch doesn't power attachment)
        if ( neighbor.id == REDSTONE_TORCH ) {
            // rotation 0: attached below, so torch above (dir.y==1) is attached
            // rotation 4: attached left, so torch to the right (dir.x==1) is attached
            // rotation 5: attached front, so torch behind (dir.z==1) is attached
            // rotation 6: attached right, so torch to the left (dir.x==-1) is attached
            // rotation 7: attached back, so torch in front (dir.z==-1) is attached
            if ( ( neighbor.rotation == 0 && dir.y == 1 ) ||
                 ( neighbor.rotation == 4 && dir.x == 1 ) ||
                 ( neighbor.rotation == 5 && dir.z == 1 ) ||
                 ( neighbor.rotation == 6 && dir.x == -1 ) ||
                 ( neighbor.rotation == 7 && dir.z == -1 ) ) {
                continue;
            }
        }
        const Block *neighbor_block = block_definition_get_definition( neighbor.id );
        if ( neighbor_block->is_redstone_dust ) {
            continue;
        }
        // A repeater feeds dust only through its output cell — before the
        // generic power <= 1 read, because a powered repeater stores 15 and
        // would otherwise feed dust from every side.
        if ( neighbor_block->is_repeater ) {
            if ( repeater_points_into( neighbor, pos + dir, pos ) && neighbor.current_redstone_power > max ) {
                max = neighbor.current_redstone_power;
            }
            continue;
        }
        if ( !neighbor_block->transmits_redstone_power && neighbor.current_redstone_power <= 1 ) {
            continue;
        }
        if ( neighbor.current_redstone_power > max ) {
            max = neighbor.current_redstone_power;
        }
    }
    return max;
}

// Appends the positions of dust that feed power directly into the dust at
// `pos`: horizontal neighbors whose shape points at `pos`, plus the vertical
// connections over solid blocks and under non-solid ones.
static void dust_feeders( World &world, const glm::ivec3 &pos, std::vector<glm::ivec3> &feeders ) {
    static constexpr glm::ivec3 horiz_dirs[ 4 ] = {
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    // A solid block directly above pos pinches every diagonal that climbs
    // over a neighbor: the corner cell must stay open for power to step up.
    const BlockState head_state = world.get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
    const bool covered = head_state.id != LAST_BLOCK_ID && block_definition_get_definition( head_state.id )->is_solid;
    for ( const glm::ivec3 &dir : horiz_dirs ) {
        glm::ivec3 neighbor_pos = pos + dir;
        BlockState neighbor = world.get_loaded_block( neighbor_pos );
        if ( neighbor.id == LAST_BLOCK_ID ) {
            continue;
        }
        const Block *neighbor_block = block_definition_get_definition( neighbor.id );
        if ( neighbor_block->is_redstone_dust && dust_points_toward( neighbor.display_id, -dir ) ) {
            feeders.push_back( neighbor_pos );
        }
        if ( neighbor_block->is_solid ) {
            // Up: dust on top of a solid neighbor feeds pos while the corner
            // above pos is open
            if ( !covered ) {
                glm::ivec3 above_pos = neighbor_pos + glm::ivec3( 0, 1, 0 );
                BlockState above = world.get_loaded_block( above_pos );
                if ( above.id != LAST_BLOCK_ID && block_definition_get_definition( above.id )->is_redstone_dust && dust_points_toward( above.display_id, -dir ) ) {
                    feeders.push_back( above_pos );
                }
            }
        } else {
            // Down: dust below an open side cell feeds pos
            glm::ivec3 below_pos = neighbor_pos + glm::ivec3( 0, -1, 0 );
            BlockState below = world.get_loaded_block( below_pos );
            if ( below.id != LAST_BLOCK_ID && block_definition_get_definition( below.id )->is_redstone_dust && dust_points_toward( below.display_id, -dir ) ) {
                feeders.push_back( below_pos );
            }
        }
    }
}

// Computes the power the dust at `pos` should have: the strongest source
// reachable through the dust connection graph, minus one per hop.
// Other dust's stored power is deliberately ignored: it is stale while a power
// change is still propagating, and reading it let loops of dust keep each
// other powered (or flicker) instead of settling.
static int compute_dust_power( World &world, const glm::ivec3 &pos ) {
    std::unordered_map<glm::ivec3, int> distances;
    std::queue<glm::ivec3> pending;
    std::vector<glm::ivec3> feeders;
    distances.emplace( pos, 0 );
    pending.push( pos );
    int best = 0;
    while ( !pending.empty( ) ) {
        glm::ivec3 cur = pending.front( );
        pending.pop( );
        int dist = distances[ cur ];
        int contribution = dust_adjacent_source_power( world, cur ) - ( dist + 1 );
        if ( contribution > best ) {
            best = contribution;
            if ( best >= REDSTONE_TORCH_POWER - 1 ) {
                break; // Dust can't get more power than this
            }
        }
        // A dust one hop further out can contribute at most 15 - (dist + 2)
        if ( dist + 2 >= REDSTONE_TORCH_POWER ) {
            continue;
        }
        feeders.clear( );
        dust_feeders( world, cur, feeders );
        for ( const glm::ivec3 &feeder : feeders ) {
            if ( distances.emplace( feeder, dist + 1 ).second ) {
                pending.push( feeder );
            }
        }
    }
    return best > 0 ? best : 0;
}

static inline const Block *next_to_block_def( World &world, const glm::ivec3 &pos ) {
    const BlockID id = world.get_loaded_block( pos ).id;
    return block_definition_get_definition( id == LAST_BLOCK_ID ? AIR : id );
}

#define NEXT_TO_STATE( i, j, k ) world.get_loaded_block( glm::ivec3( affecting_block_pos.x + i, affecting_block_pos.y + j, affecting_block_pos.z + k ) )
#define NEXT_TO_BLOCK( i, j, k ) next_to_block_def( world, glm::ivec3( affecting_block_pos.x + i, affecting_block_pos.y + j, affecting_block_pos.z + k ) )

inline int get_rotated_face( int face, int rotation ) {
    int result = face;
    for ( int i = 0; i < rotation; i++ ) {
        result = FACE_ROTATE_90[ result ];
    }
    return result;
}

// The piston base a head at block_pos is attached to: the cell behind it
// holds a piston still pointing at the head. Returns block_pos itself for an
// orphaned head — callers test `!= block_pos` for "attached".
glm::ivec3 piston_head_base( World &world, const glm::ivec3 &block_pos, const BlockState &block_state ) {
    const glm::ivec3 base_pos = block_pos - piston_facing_dir( block_state.rotation );
    const BlockState base_state = world.get_loaded_block( base_pos );
    if ( base_state.id != LAST_BLOCK_ID && block_definition_get_definition( base_state.id )->is_piston && base_pos + piston_facing_dir( base_state.rotation ) == block_pos ) {
        return base_pos;
    }
    return block_pos;
}

// Returns false if a block with the given state couldn't survive at block_pos
// (no required solid face/attachment). Used to reject invalid placements and
// to break blocks whose support was removed.
bool block_can_survive_at( World &world, const glm::ivec3 &block_pos, const BlockState &block_state ) {
    const Block *block = block_definition_get_definition( block_state.id );
    if ( block->is_door ) {
        // Doors are a lower+upper pair: the lower needs a solid floor below
        // and its partner upper above; the upper needs its partner lower
        // below. Breaking either half therefore pops the other through the
        // neighbour-update kill path.
        const glm::ivec3 partner_pos = block_pos + ( block->is_door_upper ? glm::ivec3( 0, -1, 0 ) : glm::ivec3( 0, 1, 0 ) );
        const BlockState partner_state = world.get_loaded_block( partner_pos );
        if ( partner_state.id != block->door_partner ) {
            return false;
        }
        if ( block->is_door_upper ) {
            return true;
        }
        const BlockState below = world.get_loaded_block( block_pos + glm::ivec3( 0, -1, 0 ) );
        return below.id != LAST_BLOCK_ID && block_definition_get_definition( below.id )->collides_with_player;
    }
    if ( block_is_tall_plant( block ) ) {
        // Tall plants are a lower+upper pair (tall grass): the upper needs
        // its partner lower below and is then fine; the lower needs its
        // partner upper above and falls through to the generic checks (its
        // solid ground below). Breaking either half therefore pops the
        // other through the neighbour-update kill path.
        const glm::ivec3 partner_pos = block_pos + ( block->is_tall_plant_upper ? glm::ivec3( 0, -1, 0 ) : glm::ivec3( 0, 1, 0 ) );
        const BlockState partner_state = world.get_loaded_block( partner_pos );
        if ( partner_state.id != block->tall_plant_partner ) {
            return false;
        }
        if ( block->is_tall_plant_upper ) {
            return true;
        }
    }
    if ( block->bed_partner != LAST_BLOCK_ID ) {
        // Beds are a foot+head pair like doors but horizontal: the foot's
        // partner head sits one block in the yaw direction. Breaking either
        // half pops the other through the neighbour-update kill path. Both
        // halves still need their own floor support, so fall through.
        const glm::ivec3 partner_pos = block_pos + block_bed_partner_dir( block, block_state.rotation );
        const BlockState partner_state = world.get_loaded_block( partner_pos );
        if ( partner_state.id != block->bed_partner ) {
            return false;
        }
    }
    if ( block->is_piston_head ) {
        // A head survives only with a piston base directly behind it that is
        // still pointing at this cell.
        return piston_head_base( world, block_pos, block_state ) != block_pos;
    }
    if ( block->is_torch && block_state.rotation >= 4 ) {
        // Side-mounted torch: check the attachment block is still solid.
        // rotation 4=attached LEFT(solid at x-1), 5=FRONT(z-1), 6=RIGHT(x+1), 7=BACK(z+1)
        // The formula checks block_pos - FACE_DIR_OFFSETS[face], so we need the OPPOSITE face:
        //   solid at x-1 → need face with offset +1 → FACE_RIGHT
        //   solid at z-1 → need face with offset +1 → FACE_FRONT
        //   solid at x+1 → need face with offset -1 → FACE_LEFT
        //   solid at z+1 → need face with offset -1 → FACE_BACK
        static const int rot_to_face[ 4 ] = { FACE_RIGHT, FACE_BACK, FACE_LEFT, FACE_FRONT };
        int attach_face = rot_to_face[ block_state.rotation - 4 ];
        BlockID next_to_block_id = world.get_loaded_block( glm::ivec3( block_pos.x - FACE_DIR_X_OFFSETS[ attach_face ], block_pos.y - FACE_DIR_Y_OFFSETS[ attach_face ], block_pos.z - FACE_DIR_Z_OFFSETS[ attach_face ] ) ).id;
        return next_to_block_id != LAST_BLOCK_ID && block_definition_get_definition( next_to_block_id )->collides_with_player;
    }
    if ( block->is_rail ) {
        // Rails need a colliding block below — checked directly rather than
        // through the generic needs_place_on_any_solid path, which rotates
        // the required support face by rotation: a rail's rotation is its
        // shape (0-5), not a yaw, so rotations 2-5 would look beside/above.
        const BlockState below = world.get_loaded_block( block_pos + glm::ivec3( 0, -1, 0 ) );
        return below.id != LAST_BLOCK_ID && block_definition_get_definition( below.id )->collides_with_player;
    }
    if ( block->is_crop ) {
        // Crops survive only on farmland — tilling away or trampling the
        // field pops the plant through the neighbour-update kill path.
        const BlockState below = world.get_loaded_block( block_pos + glm::ivec3( 0, -1, 0 ) );
        return below.id == FARMLAND;
    }
    return block_supports_present( world, block_pos, block_state );
}

// The generic needs_place_on_any_solid support check on its own — also used
// by the tall-plant lower placement test, where the partner upper isn't
// written yet so block_can_survive_at can't run.
bool block_supports_present( World &world, const glm::ivec3 &block_pos, const BlockState &block_state ) {
    const Block *block = block_definition_get_definition( block_state.id );
    bool block_is_ok_to_place = true;
    for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
        // Cake's rotation is the bite count, not a yaw — its support face
        // stays unrotated or a partly-eaten cake would check a side face.
        int rotated_face = get_rotated_face( face, block->is_cake ? 0 : block_state.rotation );
        if ( block->needs_place_on_any_solid[ rotated_face ] ) {
            BlockID next_to_block_id = world.get_loaded_block( glm::ivec3( block_pos.x - FACE_DIR_X_OFFSETS[ face ], block_pos.y - FACE_DIR_Y_OFFSETS[ face ], block_pos.z - FACE_DIR_Z_OFFSETS[ face ] ) ).id;
            if ( block->needs_place_on_solid_but_can_stack_on_self && next_to_block_id == block_state.id ) {
                block_is_ok_to_place = true;
                break;
            } else {
                block_is_ok_to_place = block->can_float_on_water ? next_to_block_id == WATER
                                                               : next_to_block_id != LAST_BLOCK_ID && block_definition_get_definition( next_to_block_id )->collides_with_player;
                if ( block_is_ok_to_place ) {
                    break;
                }
            }
        }
    }
    return block_is_ok_to_place;
}

// Returns true if a button/pressure plate at activator_pos is attached to
// target_pos. Uses the same needs_place_on_any_solid logic as
// block_can_survive_at: for each required face, the supporting block sits at
// activator_pos - FACE_DIR_OFFSETS[face].
static bool activator_attached_to( const glm::ivec3 &activator_pos, const BlockState &activator_state, const glm::ivec3 &target_pos ) {
    if ( activator_state.id == LAST_BLOCK_ID ) {
        return false;
    }
    const Block *activator = block_definition_get_definition( activator_state.id );
    for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
        int rotated_face = get_rotated_face( face, activator_state.rotation );
        if ( activator->needs_place_on_any_solid[ rotated_face ] ) {
            if ( activator_pos.x - FACE_DIR_X_OFFSETS[ face ] == target_pos.x && //
                 activator_pos.y - FACE_DIR_Y_OFFSETS[ face ] == target_pos.y && //
                 activator_pos.z - FACE_DIR_Z_OFFSETS[ face ] == target_pos.z ) {
                return true;
            }
        }
    }
    return false;
}

// The power a solid block or mechanism at `pos` draws directly from its
// six face neighbours: hard sources (a redstone block's initial power, a
// powered torch below, an attached pressed button/plate) up to
// REDSTONE_TORCH_POWER; when soft_ok (affected_by_redstone_power) and the
// hard read is <= 1, soft sources instead decide — powered dust pointing
// in or above, or a side torch not attached to this cell; and when
// is_mechanism, any adjacent emitting block activates it (a powered block
// feeds mechanisms whether its power is weak or strong, but powered
// mechanisms never count — they would latch each other on forever). Never
// reads the cell's own stored power. Extracted from perform_checks so
// powered_rail_chain_powered can re-run the same scan off the event path.
static int block_neighbor_power( World &world, const glm::ivec3 &pos, bool soft_ok, bool is_mechanism ) {
    // Solid block: hard power (>1) from redstone blocks, a torch below,
    // or an attached pressed button/plate; soft power (1) from dust
    // pointing in or a torch's sides; mechanisms activate next to any
    // powered block. Only solid blocks hold power — non-solids (air,
    // water, glass, foliage) stay 0 so they can't act as a power source.
    // Mechanisms that aren't solid (pistons) still activate here but
    // are capped below so they never transmit power themselves.
    int new_power = 0;
    static constexpr glm::ivec3 directions[ 6 ] = {
        glm::ivec3( 0, 1, 0 ),  //
        glm::ivec3( 0, -1, 0 ), //
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    // Check for hard power sources:
    // - Redstone block (any direction): hard power 15
    // - Torch below (dir.y == -1): hard power 15 (torch strongly powers block above)
    // - Torch above (dir.y == 1) with rotation 0: skip (torch doesn't power attachment)
    // - Torch to the side attached to this block: skip (doesn't power attachment)
    // Torch to the side NOT attached to this block: soft power, handled below.
    for ( const glm::ivec3 &dir : directions ) {
        BlockState neighbor = world.get_loaded_block( pos + dir );
        if ( neighbor.id == LAST_BLOCK_ID ) {
            continue;
        }
        const Block *neighbor_block = block_definition_get_definition( neighbor.id );
        if ( neighbor_block->initial_redstone_power > 0 ) {
            // Redstone block: hard power at source level
            if ( neighbor_block->initial_redstone_power > new_power ) {
                new_power = neighbor_block->initial_redstone_power;
            }
        }
        if ( neighbor.id == REDSTONE_TORCH && neighbor.current_redstone_power > 0 ) {
            // Check if this torch is attached to this block (skip if so)
            // Torch attachment offset: rotation 0=(0,-1,0), 4=(-1,0,0), 5=(0,0,-1), 6=(1,0,0), 7=(0,0,1)
            // Torch is attached to this block if attachment_offset == -dir
            bool is_attached = false;
            if ( neighbor.rotation == 0 && dir.y == 1 ) {
                is_attached = true;
            } else if ( neighbor.rotation == 4 && dir.x == 1 ) {
                is_attached = true;
            } else if ( neighbor.rotation == 5 && dir.z == 1 ) {
                is_attached = true;
            } else if ( neighbor.rotation == 6 && dir.x == -1 ) {
                is_attached = true;
            } else if ( neighbor.rotation == 7 && dir.z == -1 ) {
                is_attached = true;
            }
            if ( is_attached ) {
                continue; // Torch doesn't power its attachment block
            }
            if ( dir.y == -1 ) {
                // Torch below: strongly powers this block (hard power 15)
                if ( REDSTONE_TORCH_POWER > new_power ) {
                    new_power = REDSTONE_TORCH_POWER;
                }
            }
            // Torch to the side (not attached): weakly powers (soft power 1), handled below
        }
        if ( ( neighbor_block->is_button || neighbor_block->is_pressure_plate ) && neighbor.current_redstone_power > 0 ) {
            // Pressed button/plate: strongly powers the block it's attached to.
            if ( activator_attached_to( pos + dir, neighbor, pos ) && REDSTONE_TORCH_POWER > new_power ) {
                new_power = REDSTONE_TORCH_POWER;
            }
        }
        // A powered repeater strongly powers only the cell it points into.
        if ( neighbor_block->is_repeater && repeater_points_into( neighbor, pos + dir, pos ) && REDSTONE_TORCH_POWER > new_power ) {
            new_power = REDSTONE_SOURCE_POWER;
        }
    }

    if ( new_power <= 1 && soft_ok ) {
        new_power = 0;
        // Check for soft power (dust adjacent, or torch to the side not attached)
        for ( const glm::ivec3 &dir : directions ) {
            BlockState neighbor = world.get_loaded_block( pos + dir );
            if ( neighbor.id == LAST_BLOCK_ID ) {
                continue;
            }
            const Block *neighbor_block = block_definition_get_definition( neighbor.id );
            if ( neighbor_block->is_redstone_dust && neighbor.current_redstone_power > 0 ) {
                // Dust above (dir.y==1) always powers the block below it, regardless of shape.
                // Dust to the side (dir.y==0) only powers if its shape points toward this block.
                // Dust below (dir.y==-1) never powers the block above it.
                if ( dir.y == 1 || ( dir.y == 0 && dust_points_toward( neighbor.display_id, -dir ) ) ) {
                    new_power = 1; // Soft power from dust
                    break;
                }
            }
            // Torch to the side (not attached to this block) gives soft power
            if ( neighbor.id == REDSTONE_TORCH && neighbor.current_redstone_power > 0 ) {
                bool is_attached = false;
                if ( neighbor.rotation == 0 && dir.y == 1 ) {
                    is_attached = true;
                } else if ( neighbor.rotation == 4 && dir.x == 1 ) {
                    is_attached = true;
                } else if ( neighbor.rotation == 5 && dir.z == 1 ) {
                    is_attached = true;
                } else if ( neighbor.rotation == 6 && dir.x == -1 ) {
                    is_attached = true;
                } else if ( neighbor.rotation == 7 && dir.z == -1 ) {
                    is_attached = true;
                }
                if ( !is_attached && dir.y == 0 ) {
                    new_power = 1; // Soft power from torch side
                    break;
                }
            }
        }
        // Mechanisms (pistons, lamps) also activate when an adjacent
        // block is powered at all — a powered block feeds mechanism
        // components whether its power is weak or strong. Only blocks
        // that emit power count: powered solids, pressed buttons and
        // plates. A powered mechanism (lit lamp, extended piston) is
        // activated, not emitting — counting it would let two adjacent
        // mechanisms latch each other on forever after their source is
        // gone. Dust and torches have their own directional rules
        // above.
        if ( new_power == 0 && is_mechanism ) {
            for ( const glm::ivec3 &dir : directions ) {
                const BlockState neighbor = world.get_loaded_block( pos + dir );
                if ( neighbor.id == LAST_BLOCK_ID || neighbor.current_redstone_power == 0 ) {
                    continue;
                }
                const Block *neighbor_block = block_definition_get_definition( neighbor.id );
                if ( ( neighbor_block->is_solid && !neighbor_block->is_redstone_mechanism ) || neighbor_block->is_button || neighbor_block->is_pressure_plate ) {
                    new_power = 1;
                    break;
                }
            }
        }
    }
    return new_power;
}

int mechanism_direct_power( World &world, const glm::ivec3 &pos ) {
    const int power = block_neighbor_power( world, pos, true, true );
    return power > 1 ? 1 : power;
}

// Whether dust draws a connection to a same-y neighbour: the generic
// connects_to_redstone_dust flag, restricted for repeaters to the two
// cells on their facing axis (x_axis = the dust-side direction is +x).
// Repeater facing bit 0 selects the axis: rotations 0/2 lie on z, 1/3 on x.
static bool dust_connects_side( const Block *neighbor_def, const BlockState &neighbor, bool x_axis ) {
    if ( !neighbor_def->connects_to_redstone_dust ) {
        return false;
    }
    if ( neighbor_def->is_repeater ) {
        return ( neighbor.rotation & 1 ) == ( x_axis ? 1 : 0 );
    }
    return true;
}

void perform_checks( BlockUpdateQueue &blockUpdateQueue, World &world, long tick_number, //
                     const glm::ivec3 &block_pos,                                        //
                     const glm::ivec3 &affecting_block_pos ) {

    // The updating block  was next to the updating one, and might need to change
    const BlockState updateing_block_state = world.get_loaded_block( block_pos );
    const Block *updateing_block = block_definition_get_definition( updateing_block_state.id == LAST_BLOCK_ID ? AIR : updateing_block_state.id );

    // The affecting block is the one that origionally change
    const BlockState affecting_block_state = world.get_loaded_block( affecting_block_pos );
    const Block *affecting_block = block_definition_get_definition( affecting_block_state.id == LAST_BLOCK_ID ? AIR : affecting_block_state.id );

    // The state change (if any) to apply to the affecting block. Display and
    // power updates are merged into one event so a stale payload can't undo
    // the other change when it applies.
    BlockState new_block_state = affecting_block_state;
    {
        if ( affecting_block->is_redstone_dust ) {
            BlockID connects_left_id = NEXT_TO_STATE( 1, 0, 0 ).id;
            BlockID connects_right_id = NEXT_TO_STATE( -1, 0, 0 ).id;
            BlockID connects_front_id = NEXT_TO_STATE( 0, 0, 1 ).id;
            BlockID connects_back_id = NEXT_TO_STATE( 0, 0, -1 ).id;
            if ( connects_left_id == LAST_BLOCK_ID || connects_right_id == LAST_BLOCK_ID || connects_front_id == LAST_BLOCK_ID || connects_back_id == LAST_BLOCK_ID ) {
                pr_debug( "Not ready" );
            } else {
                bool connects_left = dust_connects_side( NEXT_TO_BLOCK( 1, 0, 0 ), NEXT_TO_STATE( 1, 0, 0 ), true );
                bool connects_right = dust_connects_side( NEXT_TO_BLOCK( -1, 0, 0 ), NEXT_TO_STATE( -1, 0, 0 ), true );
                bool connects_front = dust_connects_side( NEXT_TO_BLOCK( 0, 0, 1 ), NEXT_TO_STATE( 0, 0, 1 ), false );
                bool connects_back = dust_connects_side( NEXT_TO_BLOCK( 0, 0, -1 ), NEXT_TO_STATE( 0, 0, -1 ), false );

                // Vertical diagonal connections are gated by the corner cell
                // between the two dusts. The edge to pos+dir+up exists iff
                // the cell above pos is open (the upper dust sees down through
                // it, and pos can only climb a solid side block while it's
                // open); the edge to pos+dir+down exists iff the side cell
                // itself is open. A solid corner cuts the wire.
                const BlockState above_state = NEXT_TO_STATE( 0, 1, 0 );
                const bool gap_above = above_state.id == LAST_BLOCK_ID || !block_definition_get_definition( above_state.id )->is_solid;

                if ( !connects_left ) {
                    const Block *nb = NEXT_TO_BLOCK( 1, 0, 0 );
                    if ( NEXT_TO_BLOCK( 1, 1, 0 )->is_redstone_dust && gap_above ) {
                        connects_left = true;
                    } else if ( NEXT_TO_BLOCK( 1, -1, 0 )->is_redstone_dust && !nb->is_solid ) {
                        connects_left = true;
                    }
                }
                if ( !connects_right ) {
                    const Block *nb = NEXT_TO_BLOCK( -1, 0, 0 );
                    if ( NEXT_TO_BLOCK( -1, 1, 0 )->is_redstone_dust && gap_above ) {
                        connects_right = true;
                    } else if ( NEXT_TO_BLOCK( -1, -1, 0 )->is_redstone_dust && !nb->is_solid ) {
                        connects_right = true;
                    }
                }
                if ( !connects_front ) {
                    const Block *nb = NEXT_TO_BLOCK( 0, 0, 1 );
                    if ( NEXT_TO_BLOCK( 0, 1, 1 )->is_redstone_dust && gap_above ) {
                        connects_front = true;
                    } else if ( NEXT_TO_BLOCK( 0, -1, 1 )->is_redstone_dust && !nb->is_solid ) {
                        connects_front = true;
                    }
                }
                if ( !connects_back ) {
                    const Block *nb = NEXT_TO_BLOCK( 0, 0, -1 );
                    if ( NEXT_TO_BLOCK( 0, 1, -1 )->is_redstone_dust && gap_above ) {
                        connects_back = true;
                    } else if ( NEXT_TO_BLOCK( 0, -1, -1 )->is_redstone_dust && !nb->is_solid ) {
                        connects_back = true;
                    }
                }

                BlockID new_display_block_id;
                if ( connects_left && connects_right && connects_front && connects_back ) {
                    new_display_block_id = REDSTONE_WIRE_CROSS;

                } else if ( connects_left && connects_front && connects_right ) {
                    new_display_block_id = REDSTONE_WIRE_T_B;
                } else if ( connects_left && connects_front && connects_back ) {
                    new_display_block_id = REDSTONE_WIRE_T_R;
                } else if ( connects_right && connects_back && connects_left ) {
                    new_display_block_id = REDSTONE_WIRE_T_F;
                } else if ( connects_right && connects_back && connects_front ) {
                    new_display_block_id = REDSTONE_WIRE_T_L;

                } else if ( connects_left && connects_front ) {
                    new_display_block_id = REDSTONE_WIRE_L_Q2;
                } else if ( connects_right && connects_back ) {
                    new_display_block_id = REDSTONE_WIRE_L_Q4;
                } else if ( connects_right && connects_front ) {
                    new_display_block_id = REDSTONE_WIRE_L_Q1;
                } else if ( connects_left && connects_back ) {
                    new_display_block_id = REDSTONE_WIRE_L_Q3;
                } else if ( connects_left || connects_right ) {
                    new_display_block_id = REDSTONE_WIRE_LINE_1;
                } else if ( connects_front || connects_back ) {
                    new_display_block_id = REDSTONE_WIRE_LINE_2;
                } else {
                    // No connections: preserve dot state if toggled, otherwise default to cross
                    new_display_block_id = ( affecting_block_state.display_id == REDSTONE_WIRE_DOT ) ? REDSTONE_WIRE_DOT : REDSTONE_WIRE_CROSS;
                }

                new_block_state.display_id = new_display_block_id;
            }
        } else if ( affecting_block->is_rail ) {
            // A rail re-resolves its shape on every neighbour change — the
            // same kind of merge dust does, just across rotation +
            // display_id together (rail_resolve_shape owns both). Unloaded
            // neighbours leave the state untouched ("not ready").
            const BlockState resolved = rail_resolve_shape( world, affecting_block_pos, affecting_block_state, -1 );
            new_block_state.rotation = resolved.rotation;
            new_block_state.display_id = resolved.display_id;
        }
    }

    {
        int new_power;
        if ( affecting_block->initial_redstone_power ) {
            // Redstone block: always emits power
            new_power = affecting_block->initial_redstone_power;
        } else if ( affecting_block_state.id == REDSTONE_TORCH ) {
            // Torch turns off when its attachment block is powered at all (weakly OR strongly).
            // Redstone dust pointing into the attachment block weakly powers it, which DOES turn off the torch.
            // The torch does NOT power its own attachment block, so no self-feedback.
            // Attachment direction: rotation 0=below, 4=left, 5=front, 6=right, 7=back
            glm::ivec3 attach_pos = affecting_block_pos;
            if ( affecting_block_state.rotation == 0 ) {
                attach_pos.y -= 1;
            } else if ( affecting_block_state.rotation == 4 ) {
                attach_pos.x -= 1;
            } else if ( affecting_block_state.rotation == 5 ) {
                attach_pos.z -= 1;
            } else if ( affecting_block_state.rotation == 6 ) {
                attach_pos.x += 1;
            } else if ( affecting_block_state.rotation == 7 ) {
                attach_pos.z += 1;
            }
            BlockState attach_state = world.get_loaded_block( attach_pos );
            // Any power (> 0) to the attachment block turns the torch off: hard power (> 1) from a
            // torch/redstone block, or soft power (1) from dust pointing into the block.
            new_power = attach_state.current_redstone_power > 0 ? 0 : REDSTONE_TORCH_POWER;
        } else if ( affecting_block->is_button || affecting_block->is_pressure_plate ) {
            // Buttons and pressure plates own their power through their press
            // state — neighboring redstone can't change it.
            new_power = affecting_block_state.current_redstone_power;
        } else if ( affecting_block->is_repeater ) {
            // Repeaters own their power too: only RepeaterEvent flips it,
            // after the cell's delay.
            new_power = affecting_block_state.current_redstone_power;
        } else if ( affecting_block->transmits_redstone_power ) {
            // Dust: power = strongest source reachable through the dust
            // connection graph, minus one per hop.
            new_power = compute_dust_power( world, affecting_block_pos );
        } else if ( affecting_block->is_solid || affecting_block->is_redstone_mechanism ) {
            // Solid blocks hold real power; mechanisms draw from the same
            // neighbourhood scan and are capped to "activated" (1) below so
            // they never emit power themselves.
            new_power = block_neighbor_power( world, affecting_block_pos, //
                                              affecting_block->affected_by_redstone_power, affecting_block->is_redstone_mechanism );
            // A powered rail with no direct activation still powers when a
            // directly-activated powered rail sits within 8 connected hops
            // along the track (the BFS reads only direct activation, so a
            // powered loop can't hold itself on).
            if ( new_power == 0 && affecting_block_state.id == POWERED_RAIL && powered_rail_chain_powered( world, affecting_block_pos ) ) {
                new_power = 1;
            }
            // A powered mechanism doesn't emit power of its own, so cap the
            // stored level at "activated": otherwise dust would read the
            // mechanism as a hard power source.
            if ( affecting_block->is_redstone_mechanism && new_power > 1 ) {
                new_power = 1;
            }
        } else {
            // Non-solid blocks (air, water, foliage) are never powered.
            new_power = 0;
        }

        new_block_state.current_redstone_power = new_power;
    }

    if ( !BlockStates_equal( new_block_state, affecting_block_state ) ) {
        // pr_debug( "Queueing event1 with old:%d new:%d", affecting_block_state.current_redstone_power, new_block_state.current_redstone_power );
        blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( tick_number + REDSTONE_DELAY, affecting_block_pos, new_block_state, true );
    }

    if ( !block_can_survive_at( world, block_pos, updateing_block_state ) ) {
        // Support-loss breaks drop the broken block's items in survival.
        blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( tick_number, block_pos, BLOCK_STATE_AIR, false, BreakDrops::Survival );
    }

    // Gravity: a falls_with_gravity block over a loaded non-colliding cell
    // releases as a falling body. Queued 2 ticks out so a quick support
    // restore wins; the event re-checks live state when it fires.
    if ( updateing_block->falls_with_gravity ) {
        const BlockState below = world.get_loaded_block( block_pos + glm::ivec3( 0, -1, 0 ) );
        if ( below.id != LAST_BLOCK_ID && !block_definition_get_definition( below.id )->collides_with_player ) {
            blockUpdateQueue.addBlockUpdate<FallingBlockEvent>( tick_number + 2, block_pos );
        }
    }
}

// A local xorshift for the decay-check jitter — rand() is for gameplay
// randomness callers can seed, and this stream must stay uncorrelated.
static uint32_t leaf_decay_rng_state = 0x2545F491u;

void BlockNextToChangeEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {

    perform_checks( blockUpdateQueue, repGameState.world, this->tick_number, //
                    this->block_pos,                                         //
                    this->affecting_block_pos );
    perform_checks( blockUpdateQueue, repGameState.world, this->tick_number, //
                    this->affecting_block_pos,                               //
                    this->block_pos );

    // Fluids re-evaluate on any face-neighbor change (a write, a break, a
    // level change): schedule an update for either cell holding fluid. The
    // diagonal dust wakes (manhattan 2) must not schedule — water moves
    // face-adjacently only. fluid_schedule_update no-ops on non-fluid cells
    // and dedupes per cell.
    const glm::ivec3 wake_delta = glm::abs( this->block_pos - this->affecting_block_pos );
    if ( wake_delta.x + wake_delta.y + wake_delta.z == 1 ) {
        fluid_schedule_update( repGameState, this->block_pos );
        fluid_schedule_update( repGameState, this->affecting_block_pos );
        // A repeater's input reads only its face-adjacent back cell, so the
        // same manhattan-1 wakes cover every input change; the scheduler
        // no-ops on non-repeaters and dedupes per cell.
        repeater_schedule_update( repGameState, this->block_pos );
        repeater_schedule_update( repGameState, this->affecting_block_pos );
    }

    // A changed cell may have been a natural leaf's supporting log — queue a
    // jittered decay check for either position holding natural leaves. The
    // pending set keeps at most one check per leaf.
    for ( const glm::ivec3 &pos : { this->block_pos, this->affecting_block_pos } ) {
        const BlockState state = repGameState.world.get_loaded_block( pos );
        if ( state.id == LAST_BLOCK_ID || !block_is_natural_leaf( block_definition_get_definition( state.id ) ) ) {
            continue;
        }
        if ( repGameState.pending_leaf_decay.insert( pos ).second ) {
            leaf_decay_rng_state ^= leaf_decay_rng_state << 13;
            leaf_decay_rng_state ^= leaf_decay_rng_state >> 17;
            leaf_decay_rng_state ^= leaf_decay_rng_state << 5;
            const long delay = LEAF_DECAY_MIN_TICKS +
                               (long)( leaf_decay_rng_state % ( LEAF_DECAY_MAX_TICKS - LEAF_DECAY_MIN_TICKS ) );
            blockUpdateQueue.addBlockUpdate<LeafDecayEvent>( this->tick_number + delay, pos );
        }
    }
}