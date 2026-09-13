#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset ) : BlockUpdateEvent( tick_number ), block_pos( pos + offset ), affecting_block_pos( pos ) {
    this->name = "BlockNextToChangeEvent";
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
        // Always skip a torch above - torches don't power their attachment block
        if ( dir.y == 1 && neighbor.id == REDSTONE_TORCH ) {
            continue;
        }
        const Block *neighbor_block = block_definition_get_definition( neighbor.id );
        // For dust: skip soft-powered solid blocks (power <= 1)
        if ( for_dust && !neighbor_block->transmits_redstone_power && neighbor.current_redstone_power <= 1 ) {
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
            BlockState above = world.get_loaded_block( neighbor_pos + glm::ivec3( 0, 1, 0 ) );
            if ( above.id != LAST_BLOCK_ID ) {
                const Block *above_block = block_definition_get_definition( above.id );
                if ( above_block->is_redstone_dust && above.current_redstone_power > max ) {
                    max = above.current_redstone_power;
                }
            }
        }

        // Down: non-solid block at neighbor_pos with dust below
        if ( !neighbor_block->collides_with_player && !neighbor_block->is_redstone_dust ) {
            BlockState below = world.get_loaded_block( neighbor_pos + glm::ivec3( 0, -1, 0 ) );
            if ( below.id != LAST_BLOCK_ID ) {
                const Block *below_block = block_definition_get_definition( below.id );
                if ( below_block->is_redstone_dust && below.current_redstone_power > max ) {
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
                    new_display_block_id = REDSTONE_CROSS;
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
            // Torch turns off only when its attachment block (below) is STRONGLY powered.
            // Weak power (from dust) does NOT turn off a torch.
            // The torch does NOT power its own attachment block, so no self-feedback.
            glm::ivec3 below_pos = glm::ivec3( affecting_block_pos.x, affecting_block_pos.y - 1, affecting_block_pos.z );
            BlockState below_state = world.get_loaded_block( below_pos );
            // Power > 1 means hard power (from torch/redstone block); power 1 is soft (from dust)
            new_power = below_state.current_redstone_power > 1 ? 0 : REDSTONE_TORCH_POWER;
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
            // Torch above (dir.y == 1) is skipped: torch doesn't power its attachment block.
            // Torch to the side gives soft power, handled below.
            for ( const glm::ivec3 &dir : directions ) {
                BlockState neighbor = world.get_loaded_block( affecting_block_pos + dir );
                if ( neighbor.id == LAST_BLOCK_ID ) {
                    continue;
                }
                if ( dir.y == 1 && neighbor.id == REDSTONE_TORCH ) {
                    continue; // Torch doesn't power its attachment
                }
                const Block *neighbor_block = block_definition_get_definition( neighbor.id );
                if ( neighbor_block->initial_redstone_power > 0 ) {
                    // Redstone block: hard power at source level
                    if ( neighbor_block->initial_redstone_power > new_power ) {
                        new_power = neighbor_block->initial_redstone_power;
                    }
                }
                if ( neighbor.id == REDSTONE_TORCH && neighbor.current_redstone_power > 0 ) {
                    if ( dir.y == -1 ) {
                        // Torch below: strongly powers this block (hard power 15)
                        if ( REDSTONE_TORCH_POWER > new_power ) {
                            new_power = REDSTONE_TORCH_POWER;
                        }
                    }
                    // Torch to the side: weakly powers (soft power 1), handled below
                }
            }

            if ( new_power > 1 ) {
                // Hard power: store at source level (no decay).
                // Dust will read this and decay by 1.
            } else if ( affecting_block->affected_by_redstone_power ) {
                new_power = 0;
                // Check for soft power (dust adjacent, or torch to the side)
                for ( const glm::ivec3 &dir : directions ) {
                    BlockState neighbor = world.get_loaded_block( affecting_block_pos + dir );
                    if ( neighbor.id == LAST_BLOCK_ID ) {
                        continue;
                    }
                    const Block *neighbor_block = block_definition_get_definition( neighbor.id );
                    if ( neighbor_block->is_redstone_dust && neighbor.current_redstone_power > 0 ) {
                        new_power = 1; // Soft power from dust
                        break;
                    }
                    // Torch to the side gives soft power (weak power)
                    if ( dir.y == 0 && neighbor.id == REDSTONE_TORCH && neighbor.current_redstone_power > 0 ) {
                        new_power = 1; // Soft power from torch side
                        break;
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