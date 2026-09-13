#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset ) : BlockUpdateEvent( tick_number ), block_pos( pos + offset ), affecting_block_pos( pos ) {
    this->name = "BlockNextToChangeEvent";
}

// Returns true if the given dust display_id points toward the given horizontal direction,
// i.e. the dust would weakly power a solid block in that direction.
// dir_from_dust points from the dust toward the block being powered.
// Mapping: x+1=left, x-1=right, z+1=front, z-1=back (matching the connection naming in perform_checks).
// A dot (REDSTONE_DOT) points nowhere horizontally — it only powers the block below it.
inline bool dust_points_toward( BlockID display_id, const glm::ivec3 &dir_from_dust ) {
    if ( dir_from_dust.y != 0 ) {
        return false; // vertical power is handled separately
    }
    bool points_left  = ( dir_from_dust.x == 1 );   // dust connects toward x+1
    bool points_right = ( dir_from_dust.x == -1 );   // dust connects toward x-1
    bool points_front = ( dir_from_dust.z == 1 );    // dust connects toward z+1
    bool points_back  = ( dir_from_dust.z == -1 );    // dust connects toward z-1

    switch ( display_id ) {
        case REDSTONE_CROSS:     return true;
        case REDSTONE_LINE_1:    return points_left || points_right;
        case REDSTONE_LINE_2:    return points_front || points_back;
        case REDSTONE_DUST_L_Q1: return points_right || points_front;  // x-1, z+1
        case REDSTONE_DUST_L_Q2: return points_left || points_front;   // x+1, z+1
        case REDSTONE_DUST_L_Q3: return points_left || points_back;    // x+1, z-1
        case REDSTONE_DUST_L_Q4: return points_right || points_back;   // x-1, z-1
        case REDSTONE_DUST_T_B:  return points_left || points_right || points_front;  // missing back
        case REDSTONE_DUST_T_R:  return points_left || points_front || points_back;   // missing right
        case REDSTONE_DUST_T_F:  return points_right || points_left || points_back;    // missing front
        case REDSTONE_DUST_T_L:  return points_right || points_front || points_back;   // missing left
        case REDSTONE_DOT:       return false;  // dot powers only the block below
        default:                 return true;   // fallback: power all sides
    }
}

int find_largest_redstone_power_around( World &world, const glm::ivec3 &pos, bool skip_torch_above, bool for_dust ) {
    int max = 0;

    // All 6 directions. Always skip a torch above (torch doesn't power attachment block).
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
        // For dust: skip soft-powered solid blocks (power <= 1)
        if ( for_dust && !neighbor_block->transmits_redstone_power && neighbor.current_redstone_power <= 1 ) {
            continue;
        }
        // Skip dust that doesn't point toward this position (e.g. dot, or shape doesn't connect here)
        if ( neighbor_block->is_redstone_dust && !dust_points_toward( neighbor.display_id, -dir ) ) {
            continue;
        }
        if ( neighbor.current_redstone_power > max ) {
            max = neighbor.current_redstone_power;
        }
    }

    // Horizontal neighbors: check vertical dust connections (up over solid, down below non-solid)
    static constexpr glm::ivec3 horiz_dirs[ 4 ] = {
        glm::ivec3( 1, 0, 0 ),  //
        glm::ivec3( -1, 0, 0 ), //
        glm::ivec3( 0, 0, 1 ),  //
        glm::ivec3( 0, 0, -1 ), //
    };
    for ( const glm::ivec3 &dir : horiz_dirs ) {
        glm::ivec3 neighbor_pos = pos + dir;
        BlockState neighbor = world.get_loaded_block( neighbor_pos );
        if ( neighbor.id == LAST_BLOCK_ID ) {
            continue;
        }
        const Block *neighbor_block = block_definition_get_definition( neighbor.id );

        // Up: solid block at neighbor_pos with dust on top
        if ( neighbor_block->collides_with_player ) {
            BlockState above = world.get_loaded_block( neighbor_pos + glm::ivec3( 0, 1,  0 ) );
            if ( above.id != LAST_BLOCK_ID ) {
                const Block *above_block = block_definition_get_definition( above.id );
                if ( above_block->is_redstone_dust && dust_points_toward( above.display_id, -dir ) && above.current_redstone_power > max ) {
                    max = above.current_redstone_power;
                }
            }
        }

        // Down: non-solid block at neighbor_pos with dust below
        if ( !neighbor_block->collides_with_player && !neighbor_block->is_redstone_dust ) {
            BlockState below = world.get_loaded_block( neighbor_pos + glm::ivec3( 0, -1, 0 ) );
            if ( below.id != LAST_BLOCK_ID ) {
                const Block *below_block = block_definition_get_definition( below.id );
                if ( below_block->is_redstone_dust && dust_points_toward( below.display_id, -dir ) && below.current_redstone_power > max ) {
                    max = below.current_redstone_power;
                }
            }
        }
    }
    return max;
}

#define REDSTONE_DELAY 1
#define REDSTONE_TORCH_POWER 15

#define NEXT_TO_STATE( i, j, k ) world.get_loaded_block( glm::ivec3( affecting_block_pos.x + i, affecting_block_pos.y + j, affecting_block_pos.z + k ) )
#define NEXT_TO_BLOCK( i, j, k ) block_definition_get_definition( world.get_loaded_block( glm::ivec3( affecting_block_pos.x + i, affecting_block_pos.y + j, affecting_block_pos.z + k ) ).id )

inline int get_rotated_face( int face, int rotation ) {
    int result = face;
    for ( int i = 0; i < rotation; i++ ) {
        result = FACE_ROTATE_90[ result ];
    }
    return result;
}

void perform_checks( BlockUpdateQueue &blockUpdateQueue, World &world, long tick_number, //
                     const glm::ivec3 &block_pos,                                        //
                     const glm::ivec3 &affecting_block_pos ) {

    // The updating block  was next to the updating one, and might need to change
    const BlockState updateing_block_state = world.get_loaded_block( block_pos );
    const Block *updateing_block = block_definition_get_definition( updateing_block_state.id );

    // The affecting block is the one that origionally change
    const BlockState affecting_block_state = world.get_loaded_block( affecting_block_pos );
    const Block *affecting_block = block_definition_get_definition( affecting_block_state.id );

    // Water flow needs to start when a block next to water is broken
    if ( updateing_block->flows != 0 && affecting_block->breaks_in_liquid && affecting_block_pos.y <= block_pos.y ) {
        pr_debug( "Expanding a water x:%d y:%d z:%d into x:%d y:%d z:%d ", block_pos.x, block_pos.y, block_pos.z, affecting_block_pos.x, affecting_block_pos.y, affecting_block_pos.z );
        BlockState new_block_state = updateing_block_state;
        auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( tick_number + updateing_block->flows, affecting_block_pos, new_block_state, false );
        blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
    }

    {
        if ( affecting_block->is_redstone_dust ) {
            BlockID new_display_block_id = affecting_block_state.display_id;

            BlockID connects_left_id = NEXT_TO_STATE( 1, 0, 0 ).id;
            BlockID connects_right_id = NEXT_TO_STATE( -1, 0, 0 ).id;
            BlockID connects_front_id = NEXT_TO_STATE( 0, 0, 1 ).id;
            BlockID connects_back_id = NEXT_TO_STATE( 0, 0, -1 ).id;
            if ( connects_left_id == LAST_BLOCK_ID || connects_right_id == LAST_BLOCK_ID || connects_front_id == LAST_BLOCK_ID || connects_back_id == LAST_BLOCK_ID ) {
                pr_debug( "Not ready" );
            } else {
                bool connects_left = NEXT_TO_BLOCK( 1, 0, 0 )->connects_to_redstone_dust;
                bool connects_right = NEXT_TO_BLOCK( -1, 0, 0 )->connects_to_redstone_dust;
                bool connects_front = NEXT_TO_BLOCK( 0, 0, 1 )->connects_to_redstone_dust;
                bool connects_back = NEXT_TO_BLOCK( 0, 0, -1 )->connects_to_redstone_dust;

                // Vertical connections: dust connects up over solid blocks and down below non-solid blocks
                if ( !connects_left ) {
                    const Block *nb = NEXT_TO_BLOCK( 1, 0, 0 );
                    if ( nb->collides_with_player && NEXT_TO_BLOCK( 1, 1, 0 )->is_redstone_dust ) {
                        connects_left = true;
                    } else if ( !nb->collides_with_player && !nb->is_redstone_dust && NEXT_TO_BLOCK( 1, -1, 0 )->is_redstone_dust ) {
                        connects_left = true;
                    }
                }
                if ( !connects_right ) {
                    const Block *nb = NEXT_TO_BLOCK( -1, 0, 0 );
                    if ( nb->collides_with_player && NEXT_TO_BLOCK( -1, 1, 0 )->is_redstone_dust ) {
                        connects_right = true;
                    } else if ( !nb->collides_with_player && !nb->is_redstone_dust && NEXT_TO_BLOCK( -1, -1, 0 )->is_redstone_dust ) {
                        connects_right = true;
                    }
                }
                if ( !connects_front ) {
                    const Block *nb = NEXT_TO_BLOCK( 0, 0, 1 );
                    if ( nb->collides_with_player && NEXT_TO_BLOCK( 0, 1, 1 )->is_redstone_dust ) {
                        connects_front = true;
                    } else if ( !nb->collides_with_player && !nb->is_redstone_dust && NEXT_TO_BLOCK( 0, -1, 1 )->is_redstone_dust ) {
                        connects_front = true;
                    }
                }
                if ( !connects_back ) {
                    const Block *nb = NEXT_TO_BLOCK( 0, 0, -1 );
                    if ( nb->collides_with_player && NEXT_TO_BLOCK( 0, 1, -1 )->is_redstone_dust ) {
                        connects_back = true;
                    } else if ( !nb->collides_with_player && !nb->is_redstone_dust && NEXT_TO_BLOCK( 0, -1, -1 )->is_redstone_dust ) {
                        connects_back = true;
                    }
                }

                if ( connects_left && connects_right && connects_front && connects_back ) {
                    new_display_block_id = REDSTONE_CROSS;

                } else if ( connects_left && connects_front && connects_right ) {
                    new_display_block_id = REDSTONE_DUST_T_B;
                } else if ( connects_left && connects_front && connects_back ) {
                    new_display_block_id = REDSTONE_DUST_T_R;
                } else if ( connects_right && connects_back && connects_left ) {
                    new_display_block_id = REDSTONE_DUST_T_F;
                } else if ( connects_right && connects_back && connects_front ) {
                    new_display_block_id = REDSTONE_DUST_T_L;

                } else if ( connects_left && connects_front ) {
                    new_display_block_id = REDSTONE_DUST_L_Q2;
                } else if ( connects_right && connects_back ) {
                    new_display_block_id = REDSTONE_DUST_L_Q4;
                } else if ( connects_right && connects_front ) {
                    new_display_block_id = REDSTONE_DUST_L_Q1;
                } else if ( connects_left && connects_back ) {
                    new_display_block_id = REDSTONE_DUST_L_Q3;
                } else if ( connects_left || connects_right ) {
                    new_display_block_id = REDSTONE_LINE_1;
                } else if ( connects_front || connects_back ) {
                    new_display_block_id = REDSTONE_LINE_2;
                } else {
                    // No connections: preserve dot state if toggled, otherwise default to cross
                    new_display_block_id = ( affecting_block_state.display_id == REDSTONE_DOT ) ? REDSTONE_DOT : REDSTONE_CROSS;
                }

                if ( new_display_block_id != affecting_block_state.display_id ) {
                    // pr_debug( "Dust" );

                    BlockState new_block_state = affecting_block_state;
                    new_block_state.display_id = new_display_block_id;
                    auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( tick_number + REDSTONE_DELAY, affecting_block_pos, new_block_state, true );
                    blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
                }
            }
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
        } else if ( affecting_block->transmits_redstone_power ) {
            // Dust: power = max(neighbors) - 1, skipping soft-powered solid blocks
            int maximum_power = find_largest_redstone_power_around( world, affecting_block_pos, false, true );
            new_power = maximum_power > 1 ? maximum_power - 1 : 0;
        } else {
            // Solid block: distinguish hard power (from torches above/redstone blocks) and soft power (from dust/torch sides)
            new_power = 0;
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
                BlockState neighbor = world.get_loaded_block( affecting_block_pos + dir );
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
            }

            if ( new_power > 1 ) {
                // Hard power: store at source level (no decay).
                // Dust will read this and decay by 1.
            } else if ( affecting_block->affected_by_redstone_power ) {
                new_power = 0;
                // Check for soft power (dust adjacent, or torch to the side not attached)
                for ( const glm::ivec3 &dir : directions ) {
                    BlockState neighbor = world.get_loaded_block( affecting_block_pos + dir );
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
            }
        }

        if ( affecting_block->transmits_redstone_power && new_power < affecting_block_state.current_redstone_power ) {
            // Power decreased: set to 0 so downstream blocks also lose power.
            // Power will come back from remaining sources on the next recalculation.
            new_power = 0;
        }

        if ( new_power != affecting_block_state.current_redstone_power ) {
            // pr_debug( "Queueing event1 with old:%d new:%d", affecting_block_state.current_redstone_power, new_power );
            BlockState new_block_state = affecting_block_state;
            new_block_state.current_redstone_power = new_power;
            auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( tick_number + REDSTONE_DELAY, affecting_block_pos, new_block_state, true );
            blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
        }
    }

    bool block_is_ok_to_place = true;
    if ( updateing_block->is_torch && updateing_block_state.rotation >= 4 ) {
        // Side-mounted torch: check the attachment block is still solid.
        // rotation 4=attached LEFT(solid at x-1), 5=FRONT(z-1), 6=RIGHT(x+1), 7=BACK(z+1)
        // The formula checks block_pos - FACE_DIR_OFFSETS[face], so we need the OPPOSITE face:
        //   solid at x-1 → need face with offset +1 → FACE_RIGHT
        //   solid at z-1 → need face with offset +1 → FACE_FRONT
        //   solid at x+1 → need face with offset -1 → FACE_LEFT
        //   solid at z+1 → need face with offset -1 → FACE_BACK
        static const int rot_to_face[ 4 ] = { FACE_RIGHT, FACE_BACK, FACE_LEFT, FACE_FRONT };
        int attach_face = rot_to_face[ updateing_block_state.rotation - 4 ];
        BlockID next_to_block_id = world.get_loaded_block( glm::ivec3( block_pos.x - FACE_DIR_X_OFFSETS[ attach_face ], block_pos.y - FACE_DIR_Y_OFFSETS[ attach_face ], block_pos.z - FACE_DIR_Z_OFFSETS[ attach_face ] ) ).id;
        block_is_ok_to_place = block_definition_get_definition( next_to_block_id )->collides_with_player;
    } else {
        for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
            int rotated_face = get_rotated_face( face, updateing_block_state.rotation );
            if ( updateing_block->needs_place_on_any_solid[ rotated_face ] ) {
                BlockID next_to_block_id = world.get_loaded_block( glm::ivec3( block_pos.x - FACE_DIR_X_OFFSETS[ face ], block_pos.y - FACE_DIR_Y_OFFSETS[ face ], block_pos.z - FACE_DIR_Z_OFFSETS[ face ] ) ).id;
                if ( updateing_block->needs_place_on_solid_but_can_stack_on_self && next_to_block_id == updateing_block_state.id ) {
                    block_is_ok_to_place = true;
                    break;
                } else {
                    block_is_ok_to_place = block_definition_get_definition( next_to_block_id )->collides_with_player;
                    if ( block_is_ok_to_place ) {
                        break;
                    }
                }
            }
        }
    }
    if ( !block_is_ok_to_place ) {
        auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( tick_number, block_pos, BLOCK_STATE_AIR, false );
        blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
    }
}

void BlockNextToChangeEvent::performAction( BlockUpdateQueue &blockUpdateQueue, RepGameState &repGameState ) {

    perform_checks( blockUpdateQueue, repGameState.world, this->tick_number, //
                    this->block_pos,                                         //
                    this->affecting_block_pos );
    perform_checks( blockUpdateQueue, repGameState.world, this->tick_number, //
                    this->affecting_block_pos,                               //
                    this->block_pos );
}