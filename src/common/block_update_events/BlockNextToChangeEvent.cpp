#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

BlockNextToChangeEvent::BlockNextToChangeEvent( long tick_number, const glm::ivec3 &pos, const glm::ivec3 &offset ) : BlockUpdateEvent( tick_number ), block_pos( pos + offset ), affecting_block_pos( pos ) {
    this->name = "BlockNextToChangeEvent";
}

int find_largest_redstone_power_around( World &world, const glm::ivec3 &pos ) {
    int max = 0;
    glm::ivec3 working_pos = pos;
    for ( int j = -1; j < 2; j += 2 ) {
        working_pos.y = pos.y + j;
        int new_power = world.get_loaded_block( working_pos ).current_redstone_power;
        max = new_power > max ? new_power : max;
    }
    working_pos.y = pos.y;

    for ( int i = -1; i < 2; i += 2 ) {
        working_pos.x = pos.x + i;
        int new_power = world.get_loaded_block( working_pos ).current_redstone_power;
        max = new_power > max ? new_power : max;
    }
    working_pos.x = pos.x;

    for ( int k = -1; k < 2; k += 2 ) {
        working_pos.z = pos.z + k;
        int new_power = world.get_loaded_block( working_pos ).current_redstone_power;
        max = new_power > max ? new_power : max;
    }
    // working_pos.z = pos.z;

    return max;
}

#define REDSTONE_DELAY 0

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
        if ( affecting_block->connects_to_redstone_dust ) {
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
                    auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( tick_number, affecting_block_pos, new_block_state, true );
                    blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
                }
            }
        }
    }

    {
        int new_power;
        if ( affecting_block->initial_redstone_power ) {
            new_power = affecting_block->initial_redstone_power;
        } else {
            int maximum_power = find_largest_redstone_power_around( world, affecting_block_pos );
            new_power = maximum_power > 1 ? maximum_power - 1 : 0;
            if ( !affecting_block->transmits_redstone_power ) {
                if ( new_power > 0 ) {
                    if ( affecting_block->affected_by_redstone_power ) {
                        new_power = 1;
                    } else {
                        new_power = 0;
                    }
                }
            }
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