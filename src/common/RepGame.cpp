
#include <cmath>
#include <ctime>
#include <string>
#include <unistd.h>
#include <chrono>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

BlockID RepGame::change_block( const int place, BlockState blockState ) {

    glm::ivec3 block;
    if ( place ) {
        block = globalGameState.block_selection.pos_create;
        if ( block_definition_get_definition( blockState.id )->collides_with_player ) {
            // If the block collides with the player, make sure it's not being placed where it would collide
            if ( Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos, block ) ) {
                return LAST_BLOCK_ID;
            }
        }
    } else {
        block = globalGameState.block_selection.pos_destroy;
    }
    BlockID previous_block_id = globalGameState.world.get_loaded_block( block ).id;
    // globalGameState.world.set_loaded_block( block, blockState );
    auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number, block, blockState, false );
    globalGameState.blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
    return previous_block_id;
}

unsigned char RepGame::getPlacedRotation( const BlockID blockID ) const {
    unsigned char rotation = BLOCK_ROTATE_0;
    const Block *block = block_definition_get_definition( blockID );
    if ( block->rotate_on_placement ) {
        if ( globalGameState.camera.angle_H < 45 ) {
            rotation = BLOCK_ROTATE_0;
        } else if ( globalGameState.camera.angle_H < 135 ) {
            rotation = BLOCK_ROTATE_90;
        } else if ( globalGameState.camera.angle_H < 225 ) {
            rotation = BLOCK_ROTATE_180;
        } else if ( globalGameState.camera.angle_H < 315 ) {
            rotation = BLOCK_ROTATE_270;
        } else {
            rotation = BLOCK_ROTATE_0;
        }
    }
    // Torches can be placed on the sides of blocks.
    // rotation >= 4 encodes attachment face: 4=LEFT, 5=FRONT, 6=RIGHT, 7=BACK
    if ( block->is_torch ) {
        int face = globalGameState.block_selection.face;
        if ( face == FACE_RIGHT ) {
            rotation = 4;       // attached LEFT (solid block is to the left)
        } else if ( face == FACE_FRONT ) {
            rotation = 5;       // attached FRONT (solid block is in front)
        } else if ( face == FACE_LEFT ) {
            rotation = 6;       // attached RIGHT (solid block is to the right)
        } else if ( face == FACE_BACK ) {
            rotation = 7;       // attached BACK (solid block is behind)
        }
    }
    return rotation;
}

// static bool block_places_on_all_faces( Block *block ) {
//     for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
//         if ( block->needs_place_on_any_solid[ face ] ) {
//             return false;
//         }
//     }
//     return true;
// }

void RepGame::add_to_hotbar( const bool alsoSelect, const BlockID blockId ) {
    if ( globalGameState.hotbar.getSelectedBlock( ) == blockId ) {
        // We don't support having the same block in the hotbar or inventory more than once, until there are qtys.
        return;
    }
    if ( globalGameState.hotbar.addBlock( alsoSelect, blockId ) ) {
        // The block we added might have gone into our selected slot.
        const BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
        globalGameState.ui_overlay.set_holding_block( selectedBlock );
    }
}

static bool was_middle = false;
void RepGame::process_mouse_events( ) {
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle ) {
        // Picking a block
        BlockState blockState = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        RepGame::add_to_hotbar( true, blockState.id );
        if ( !was_middle ) {
            pr_debug( "Selected block:%d rotation:%d redstone_power:%d display:%d", blockState.id, blockState.rotation, blockState.current_redstone_power, blockState.display_id );
        }
        was_middle = true;
    } else {
        was_middle = false;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left && globalGameState.input.click_delay_left == 0 ) {
        // Mining a block
        change_block( 0, BLOCK_STATE_AIR );
        // RepGame::add_to_hotbar( false, previous_block );
        globalGameState.input.click_delay_left = 30;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        // Placing a block
        BlockID holdingBlock = globalGameState.hotbar.getSelectedBlock( );
        if ( holdingBlock != LAST_BLOCK_ID ) {
            const Block *holdingBlockDef = block_definition_get_definition( holdingBlock );
            bool did_toggle_dust = false;

            // Toggle redstone dust between cross and dot when right-clicking an existing dust block with dust in hand
            if ( holdingBlockDef->is_redstone_dust ) {
                BlockState targetBlock = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
                const Block *targetBlockDef = block_definition_get_definition( targetBlock.id );
                if ( targetBlockDef->is_redstone_dust ) {
                    BlockState new_block_state = targetBlock;
                    if ( targetBlock.display_id == REDSTONE_CROSS ) {
                        new_block_state.display_id = REDSTONE_DOT;
                    } else if ( targetBlock.display_id == REDSTONE_DOT ) {
                        new_block_state.display_id = REDSTONE_CROSS;
                    } else {
                        // Has connections, can't toggle — fall through to place new dust
                        new_block_state.display_id = LAST_BLOCK_ID; // sentinel: don't toggle
                    }
                    if ( new_block_state.display_id != LAST_BLOCK_ID ) {
                        auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, new_block_state, true );
                        globalGameState.blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
                        globalGameState.input.click_delay_right = 30;
                        did_toggle_dust = true;
                    }
                }
            }

            if ( !did_toggle_dust ) {
                const unsigned char rotation = getPlacedRotation( holdingBlock );
                if ( holdingBlockDef->is_torch ) {
                    pr_debug( "Placing torch face:%d rotation:%d", globalGameState.block_selection.face, rotation );
                }
                change_block( 1, { holdingBlock, rotation, 0, holdingBlock } );
                globalGameState.input.click_delay_right = 30;
            }
        }
    }
    // if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.currentPosition.wheel_counts != globalGameState.input.mouse.previousPosition.wheel_counts ) {
    //     int wheel_diff = globalGameState.input.mouse.currentPosition.wheel_counts > globalGameState.input.mouse.previousPosition.wheel_counts ? 1 : -1;
    //     BlockState blockState = world_get_loaded_block( &globalGameState.world, globalGameState.block_selection.pos_destroy );
    //     int blockID_int = ( int )blockState.id;
    //     if ( blockID_int != LAST_BLOCK_ID ) {
    //         bool valid_scroll_block;
    //         do {
    //             blockID_int += wheel_diff;
    //             if ( blockID_int >= LAST_BLOCK_ID ) {
    //                 blockID_int = 1;
    //             }
    //             if ( blockID_int <= 0 ) {
    //                 blockID_int = LAST_BLOCK_ID - 1;
    //             }
    //             Block *next_block = block_definition_get_definition( ( BlockID )blockID_int );
    //             bool is_pickable = next_block->is_pickable;
    //             bool places_on_any_face = block_places_on_all_faces( next_block );
    //             valid_scroll_block = places_on_any_face && is_pickable;
    //         } while ( !valid_scroll_block );
    //         blockState.id = ( BlockID )blockID_int;
    //         blockState.display_id = ( BlockID )blockID_int;
    //         // blockState.rotation = getPlacedRotation( blockState.id );
    //         change_block( 0, BLOCK_STATE_AIR );
    //         change_block( 0, blockState );
    //     }
    // }
    globalGameState.input.mouse.currentPosition.wheel_counts = 0;
}

void RepGame::build_camera_view( float angle_H, float angle_V, const glm::vec3 &pos, glm::vec3 &look, glm::mat4 &rotation, glm::mat4 &view_look, glm::mat4 &view_trans ) const {
    look = glm::normalize( glm::vec3(        //
        sin( ( angle_H ) * ( M_PI / 180 ) ), //
        -tan( angle_V * ( M_PI / 180 ) ),    //
        -cos( ( angle_H ) * ( M_PI / 180 ) ) ) );

    glm::mat4 rotate = glm::rotate( glm::mat4( 1.0f ), glm::radians( -angle_H + 180 ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    rotate = glm::rotate( rotate, glm::radians( angle_V ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    rotation = rotate;

    view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                             look,                         // Look at look vector
                             glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
    );
    view_trans = glm::translate( glm::mat4( 1.0f ), -1.0f * pos );
}

void RepGame::process_camera_angle( ) {
    float upAngleLimit = 90 - 0.001f;
    if ( globalGameState.camera.angle_V > upAngleLimit ) {
        globalGameState.camera.angle_V = upAngleLimit;
    }
    float downAngleLimit = -90 + 0.001f;
    if ( globalGameState.camera.angle_V < downAngleLimit ) {
        globalGameState.camera.angle_V = downAngleLimit;
    }
    build_camera_view( globalGameState.camera.angle_H, globalGameState.camera.angle_V, globalGameState.camera.pos, //
                       globalGameState.camera.look, globalGameState.camera.rotation, globalGameState.camera.view_look, globalGameState.camera.view_trans );

    globalGameState.camera.movement.x = sinf( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * static_cast<float>( ( M_PI / 180 ) ) );
    globalGameState.camera.movement.y = -tanf( globalGameState.camera.angle_V * static_cast<float>( ( M_PI / 180 ) ) );
    globalGameState.camera.movement.z = -cosf( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * static_cast<float>( ( M_PI / 180 ) ) );
}

void RepGame::process_movement( ) {
    bool player_flying = globalGameState.input.player_flying;
    bool player_sprinting = globalGameState.input.player_sprinting;
    bool needs_ground_to_jump = !player_flying;
    float gravity = player_flying ? 0 : GRAVITY_STRENGTH;

    float movement_speed;
    if ( player_flying && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_FLYING_SPRINTING;
    } else if ( player_flying && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_FLYING;
    } else if ( !player_flying && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SPRINTING;
    } else if ( !player_flying && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_WALKING;
    } else {
        movement_speed = 0;
        pr_debug( "Invalid sprint fly combo" );
    }

    if ( needs_ground_to_jump ) {
        if ( globalGameState.input.movement.jumpPressed && globalGameState.camera.standing_on_solid && globalGameState.camera.y_speed < 0.01f && globalGameState.camera.y_speed > -0.01f ) {
            globalGameState.camera.y_speed = JUMP_STRENGTH;
        }
    } else {
        const float fly_vspeed = player_sprinting ? FLY_SPRINT_VERTICAL_SPEED : FLY_VERTICAL_SPEED;
        if ( globalGameState.input.movement.sneakPressed && globalGameState.input.movement.jumpPressed ) {
            globalGameState.camera.y_speed = 0;
        } else {
            if ( globalGameState.input.movement.sneakPressed ) {
                globalGameState.camera.y_speed = -fly_vspeed;
            } else if ( globalGameState.input.movement.jumpPressed ) {
                globalGameState.camera.y_speed = fly_vspeed;
            } else {
                globalGameState.camera.y_speed = 0;
            }
        }
    }

    float accel = -gravity;

    // Build the target horizontal velocity from input. movement.x/z are the
    // unit forward/right direction derived from the camera yaw; sizeH is the
    // analog input magnitude (0..1 on keyboard). The target is clamped to the
    // selected movement speed so acceleration can never push us past it.
    const float target_vx = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.x;
    const float target_vz = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.z;
    const glm::vec2 target_vel = glm::vec2( target_vx, target_vz );

    // Pick the rate at which horizontal_vel approaches target_vel. When input
    // is held we accelerate; when it is released we apply (usually higher)
    // friction to bring the player to rest. Flying uses a high rate so it
    // stays snappy and effectively reaches target in a single tick.
    const bool has_input = globalGameState.input.movement.sizeH > 0.001f;
    float rate;
    if ( player_flying ) {
        rate = PLAYER_FLY_ACCEL;
    } else if ( has_input ) {
        rate = globalGameState.camera.standing_on_solid ? PLAYER_GROUND_ACCEL : PLAYER_AIR_ACCEL;
    } else {
        rate = globalGameState.camera.standing_on_solid ? PLAYER_GROUND_FRICTION : PLAYER_AIR_FRICTION;
    }

    // Accelerate horizontal_vel toward target_vel, clamping the per-tick
    // velocity change to `rate` so the approach is smooth.
    glm::vec2 dvel = target_vel - globalGameState.camera.horizontal_vel;
    float dvel_len = glm::length( dvel );
    if ( dvel_len <= rate || rate <= 0.0f ) {
        globalGameState.camera.horizontal_vel = target_vel;
    } else {
        globalGameState.camera.horizontal_vel += dvel * ( rate / dvel_len );
    }

    glm::vec3 movement_vector = glm::vec3( );
    movement_vector.x = globalGameState.camera.horizontal_vel.x;
    movement_vector.y = globalGameState.camera.y_speed + accel;
    // Terminal velocity only applies to gravity-driven falling, not to flying
    // (which sets y_speed directly from input).
    if ( !player_flying ) {
        if ( movement_vector.y > TERMINAL_VELOCITY ) {
            movement_vector.y = TERMINAL_VELOCITY;
        }
        if ( movement_vector.y < -TERMINAL_VELOCITY ) {
            movement_vector.y = -TERMINAL_VELOCITY;
        }
    }
    movement_vector.z = globalGameState.camera.horizontal_vel.y;
    if ( globalGameState.input.no_clip ) {
        globalGameState.camera.standing_on_solid = 0;
    } else {
        Collision::check_move( globalGameState.world, movement_vector, //
                               globalGameState.camera.pos,             //
                               &globalGameState.camera.standing_on_solid );
    }

    globalGameState.camera.pos = globalGameState.camera.pos + movement_vector;
    globalGameState.camera.y_speed = movement_vector.y;
    // If a horizontal axis was blocked by collision, zero that component of
    // the persisted velocity so we don't keep pushing into the wall.
    globalGameState.camera.horizontal_vel.x = movement_vector.x;
    globalGameState.camera.horizontal_vel.y = movement_vector.z;
    // pr_debug("Y speed:%f",globalGameState.camera.y_speed);
}

void RepGame::process_block_updates( ) {
    // for ( size_t i = 0; i < 20; i++ ) {
    //     world_process_random_ticks( &globalGameState.world );
    // }
    globalGameState.blockUpdateQueue.processAllBlockUpdates( globalGameState, globalGameState.tick_number );
}

void RepGame::process_inventory_events( ) {
    if ( globalGameState.input.drop_item ) {
        globalGameState.input.drop_item = false;
        globalGameState.hotbar.dropSelectedItem( );
        BlockID holdingBlock = globalGameState.hotbar.getSelectedBlock( );
        globalGameState.ui_overlay.set_holding_block( holdingBlock );
    }
    if ( globalGameState.input.inventory_open ) {
        if ( globalGameState.input.mouse.buttons.left && globalGameState.input.mouse.buttons.left_click_handled == false ) {
            globalGameState.input.mouse.buttons.left_click_handled = true;
            BlockID blockId = globalGameState.main_inventory.whichBlockClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
            // pr_debug( "Clicked on %d %d blockID:%d", globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y, blockId );
            if ( blockId != LAST_BLOCK_ID ) {
                add_to_hotbar( true, blockId );
            }
        }
    } else {
        // Steal any left click events so they don't trigger right when we open the inventory.
        globalGameState.input.mouse.buttons.left_click_handled = true;
    }
}

void RepGame::tick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    globalGameState.tick_number++;

    // Snapshot the camera transform as it was at the end of the previous tick, so the
    // render loop can interpolate between this and the freshly-computed transform below.
    globalGameState.camera.prev_pos = globalGameState.camera.pos;
    globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
    globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;

    globalGameState.multiplayer.process_events( globalGameState.world );

    int wheel_diff = globalGameState.input.mouse.previousPosition.wheel_counts - globalGameState.input.mouse.currentPosition.wheel_counts;
    if ( wheel_diff != 0 ) {
        if ( globalGameState.input.inventory_open ) {
            globalGameState.main_inventory.incrementSelectedPage( wheel_diff );
        } else {
            BlockID holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
            globalGameState.ui_overlay.set_holding_block( holdingBlock );
        }
    }

    if ( RepGame::should_lock_pointer( ) ) {
        RepGame::process_mouse_events( );
        int whichFace = 0;
        glm::vec3 pos_with_reach = globalGameState.camera.pos + ( globalGameState.camera.look * static_cast<float>( REACH_DISTANCE ) );
        globalGameState.block_selection.selectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, nullptr, globalGameState.camera.pos, pos_with_reach, globalGameState.block_selection.pos_destroy, &whichFace, 0, 1, 0 );

        globalGameState.block_selection.face = whichFace;
        globalGameState.block_selection.pos_create.x = globalGameState.block_selection.pos_destroy.x + ( whichFace == FACE_RIGHT ) - ( whichFace == FACE_LEFT );
        globalGameState.block_selection.pos_create.y = globalGameState.block_selection.pos_destroy.y + ( whichFace == FACE_TOP ) - ( whichFace == FACE_BOTTOM );
        globalGameState.block_selection.pos_create.z = globalGameState.block_selection.pos_destroy.z + ( whichFace == FACE_BACK ) - ( whichFace == FACE_FRONT );

        globalGameState.world.set_selected_block( globalGameState.block_selection.pos_destroy, globalGameState.block_selection.selectionInBounds );
        const float raw_dx = static_cast<float>( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x );
        const float raw_dy = static_cast<float>( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y );
        // Low-pass filter the per-tick look delta so that sub-pixel / slow-pan motion
        // (where integer mouse deltas alternate 0,1,0,1) becomes a steady fractional
        // velocity instead of a stuttered step. Adds a small amount of input latency.
        globalGameState.input.mouse.smoothed_dx = globalGameState.input.mouse.smoothed_dx * ( 1.0f - MOUSE_SMOOTHING ) + raw_dx * MOUSE_SMOOTHING;
        globalGameState.input.mouse.smoothed_dy = globalGameState.input.mouse.smoothed_dy * ( 1.0f - MOUSE_SMOOTHING ) + raw_dy * MOUSE_SMOOTHING;
        globalGameState.camera.angle_H += globalGameState.input.mouse.smoothed_dx * MOUSE_SENSITIVITY;
        globalGameState.camera.angle_V += globalGameState.input.mouse.smoothed_dy * MOUSE_SENSITIVITY;
        if ( globalGameState.camera.angle_H >= 360.0f ) {
            globalGameState.camera.angle_H -= 360.0f;
        }
        if ( globalGameState.camera.angle_H < 0.0f ) {
            globalGameState.camera.angle_H += 360.0f;
        }
    }
    globalGameState.input.mouse.currentPosition.x = globalGameState.screen.width / 2.0f;
    globalGameState.input.mouse.currentPosition.y = globalGameState.screen.height / 2.0f;

    RepGame::process_movement( );
    RepGame::process_camera_angle( );

    if ( globalGameState.input.click_delay_right > 0 ) {
        globalGameState.input.click_delay_right--;
    } else {
        globalGameState.input.click_delay_right = 0;
    }
    if ( globalGameState.input.click_delay_left > 0 ) {
        globalGameState.input.click_delay_left--;
    } else {
        globalGameState.input.click_delay_left = 0;
    }

    globalGameState.input.mouse.previousPosition.x = globalGameState.input.mouse.currentPosition.x;
    globalGameState.input.mouse.previousPosition.y = globalGameState.input.mouse.currentPosition.y;
    globalGameState.input.mouse.previousPosition.wheel_counts = globalGameState.input.mouse.currentPosition.wheel_counts;

    RepGame::process_inventory_events( );
    RepGame::process_block_updates( );
}

void RepGame::initializeGameState( const char *world_name ) {
    globalGameState.input.exitGame = false;
    globalGameState.input.player_flying = false;
    globalGameState.input.inventory_open = false;
    globalGameState.input.no_clip = false;
    globalGameState.input.mouse.smoothed_dx = 0.0f;
    globalGameState.input.mouse.smoothed_dy = 0.0f;
    globalGameState.camera.angle_H = 0.0f;
    globalGameState.camera.angle_V = 0.0f;
    globalGameState.camera.pos.x = 0.5f;
    globalGameState.camera.pos.y = ceil( MapGen::calculateTerrainHeight( 0, 0 ) ) + PLAYER_EYE_HEIGHT + 0.5f;
    globalGameState.camera.pos.z = 0.5f;
    globalGameState.camera.y_speed = 0.0f;
    globalGameState.camera.horizontal_vel = glm::vec2( 0.0f, 0.0f );
    globalGameState.input.worldDrawQuality = WorldDrawQuality::MEDIUM;
    globalGameState.main_inventory.inventory_renderer.options.active_height_percent = 0.75f;
    globalGameState.main_inventory.inventory_renderer.options.max_height_percent = 0.75f;
    globalGameState.main_inventory.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.main_inventory.inventory_renderer.options.gravity_bottom = false;
    globalGameState.main_inventory.inventory_renderer.options.shows_selection_slot = false;

    globalGameState.hotbar.inventory_renderer.options.active_height_percent = 1.0f;
    globalGameState.hotbar.inventory_renderer.options.max_height_percent = 0.1f;
    globalGameState.hotbar.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.hotbar.inventory_renderer.options.gravity_bottom = true;
    globalGameState.hotbar.inventory_renderer.options.shows_selection_slot = true;

    globalGameState.map_storage.init( world_name );
    PlayerData saved_data;
    if ( globalGameState.map_storage.read_player_data( saved_data ) ) {
        globalGameState.camera.pos.x = saved_data.world_x;
        globalGameState.camera.pos.y = saved_data.world_y;
        globalGameState.camera.pos.z = saved_data.world_z;
        globalGameState.camera.angle_H = saved_data.angle_H;
        globalGameState.camera.angle_V = saved_data.angle_V;
        globalGameState.input.player_flying = saved_data.flying;
        globalGameState.input.no_clip = saved_data.no_clip;
        globalGameState.input.worldDrawQuality = static_cast<WorldDrawQuality>( saved_data.worldDrawQuality );
        globalGameState.hotbar.applySavedInventory( saved_data.hotbar_inventory );
        globalGameState.hotbar.setSelectedSlot( saved_data.selected_hotbar_slot );
    }
    // Seed the interpolation snapshot so the first rendered frame doesn't blend from zeroes.
    globalGameState.camera.prev_pos = globalGameState.camera.pos;
    globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
    globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;
    BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
    globalGameState.ui_overlay.set_holding_block( selectedBlock );
}

MK_TEXTURE( textures, 384, 832, 16, 16 );

static bool globalSupportsAnisotropic;

bool RepGame::supportsAnisotropic( ) {
    return globalSupportsAnisotropic;
}

RepGameState *RepGame::init( const char *world_name, const bool connect_multi, const char *host, const bool supportsAnisotropicFiltering ) {
    globalGameState.screen.width = DEFAULT_WINDOW_WIDTH;
    globalGameState.screen.height = DEFAULT_WINDOW_HEIGHT;
    globalSupportsAnisotropic = supportsAnisotropicFiltering;

    // Start broadcasting chunk updates
    if ( connect_multi ) {
        globalGameState.multiplayer.init( host, 25566 );
        // multiplayer_init( "localhost", 25566 );
    }

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBlendEquation( GL_FUNC_ADD );

    VertexBufferLayout vbl_ui_overlay_vertex;
    // These are from UIOverlayVertex
    vbl_ui_overlay_vertex.push_float( 2 );        // UIOverlayVertex screen_x, screen_y
    vbl_ui_overlay_vertex.push_float( 2 );        // UIOverlayVertex texture
    vbl_ui_overlay_vertex.push_unsigned_int( 1 ); // UIOverlayVertex is_isometric
    vbl_ui_overlay_vertex.push_unsigned_int( 1 ); // UIOverlayVertex face_type

    VertexBufferLayout vbl_ui_overlay_instance;
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance screen_x, screen_y, screen_z
    vbl_ui_overlay_instance.push_float( 2 );        // UIOverlayInstance width, height
    vbl_ui_overlay_instance.push_unsigned_int( 1 ); // UIOverlayInstance is_block
    vbl_ui_overlay_instance.push_unsigned_int( 1 ); // UIOverlayInstance is_isometric
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance id_isos
    vbl_ui_overlay_instance.push_float( 4 );        // UIOverlayInstance tint

    globalGameState.blocksTexture.init( texture_source_textures, 0 );
    block_definitions_initilize_definitions( &globalGameState.blocksTexture );

    globalGameState.main_inventory.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, MAIN_INVENTORY_WIDTH, MAIN_INVENTORY_HEIGHT );
    globalGameState.hotbar.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, HOTBAR_WIDTH, HOTBAR_HEIGHT );

    initializeGameState( world_name );

    globalGameState.world.init( globalGameState.camera.pos, globalGameState.screen.width, globalGameState.screen.height, globalGameState.map_storage );

    globalGameState.ui_overlay.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance );
    imgui_overlay_init( &globalGameState.imgui_overlay );

    BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
    globalGameState.ui_overlay.set_holding_block( selectedBlock );

    int iMultiSample = 0;
    int iNumSamples = 0;
    glGetIntegerv( GL_SAMPLE_BUFFERS, &iMultiSample );
    glGetIntegerv( GL_SAMPLES, &iNumSamples );
    pr_debug( "GL_SAMPLE_BUFFERS = %d, GL_SAMPLES = %d", iMultiSample, iNumSamples );
    return &globalGameState;
}

void RepGame::set_textures( const unsigned int which_texture, unsigned char *textures, const int textures_len ) {
    Texture::set_texture_data( which_texture, textures, textures_len );
}

bool RepGame::shouldExit( ) const {
    return globalGameState.input.exitGame;
}

bool RepGame::should_lock_pointer( ) const {
    return !globalGameState.input.inventory_open;
}
void RepGame::changeSize( const int w, const int h ) {
    // pr_debug( "Screen Size Change:%dx%d", w, h );
    if ( w == 0 || h == 0 ) {
        return;
    }
    globalGameState.screen.width = static_cast<float>( w );
    globalGameState.screen.height = static_cast<float>( h );
    glViewport( 0, 0, w, h );

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;
    globalGameState.ui_overlay.on_screen_size_change( w, h );
    globalGameState.main_inventory.onScreenSizeChange( w, h );
    globalGameState.hotbar.onScreenSizeChange( w, h );
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, 800.0f );
    globalGameState.screen.ortho = glm::ortho<float>( 0.f, w, 0.f, h, -1.f, 1.f );
    globalGameState.screen.ortho_center = glm::ortho<float>( -w / 2.0f, w / 2.0f, -h / 2.0f, h / 2.0f, -1.f, 1.f );
    globalGameState.world.change_size( w, h );
}

void RepGame::clear( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}
void RepGame::get_screen_size( int *width, int *height ) const {
    *width = globalGameState.screen.width;
    *height = globalGameState.screen.height;
}

void RepGame::draw( float alpha ) {
    static int screenshot_counter = 0;
    if ( globalGameState.input.exitGame ) {
        // Don't bother draw the state if the game is exiting
        return;
    }
    const long long t_draw_start = now_us( );
    profiling.us_render = 0;
    profiling.us_world_draw = 0;
    profiling.us_ui_draw = 0;
    profiling.num_drawable_chunks = 0;
    profiling.num_chunks_remeshed = 0;
    // Interpolate the rendered camera between the previous tick's transform (prev_*) and the
    // current tick's transform. This decouples the visual camera from the fixed-timestep
    // simulation, so panning/walking stays smooth even when the render rate exceeds UPS_RATE
    // or when per-tick input deltas (e.g. mouse motion) are non-uniform.
    if ( alpha < 0.0f ) {
        alpha = 0.0f;
    } else if ( alpha > 1.0f ) {
        alpha = 1.0f;
    }
    const glm::vec3 render_pos = glm::mix( globalGameState.camera.prev_pos, globalGameState.camera.pos, alpha );
    const float render_angle_V = glm::mix( globalGameState.camera.prev_angle_V, globalGameState.camera.angle_V, alpha );
    // angle_H wraps around 360, so interpolate along the shortest angular path.
    float angle_diff = globalGameState.camera.angle_H - globalGameState.camera.prev_angle_H;
    while ( angle_diff > 180.0f ) {
        angle_diff -= 360.0f;
    }
    while ( angle_diff < -180.0f ) {
        angle_diff += 360.0f;
    }
    float render_angle_H = globalGameState.camera.prev_angle_H + angle_diff * alpha;
    while ( render_angle_H < 0.0f ) {
        render_angle_H += 360.0f;
    }
    while ( render_angle_H >= 360.0f ) {
        render_angle_H -= 360.0f;
    }
    glm::vec3 render_look;
    glm::mat4 render_rotation;
    glm::mat4 render_view_look;
    glm::mat4 render_view_trans;
    build_camera_view( render_angle_H, render_angle_V, render_pos, render_look, render_rotation, render_view_look, render_view_trans );

    // Floating origin: snap an integer origin to the chunk grid so all
    // coordinates fed to the GPU stay near zero regardless of how far the
    // player is from the world origin. blockCoords in the VBO are
    // integer-valued floats, so subtracting the integer u_Origin in the
    // shader is exact in float32 (both < 2^24). The view translation also
    // becomes small, eliminating the large-float cancellation that caused
    // Z-fighting between the selection outline and terrain at distance.
    const glm::ivec3 renderOrigin = glm::ivec3( glm::floor( render_pos / CHUNK_SIZE_F ) ) * CHUNK_SIZE_I;
    const glm::vec3 renderOriginF = glm::vec3( renderOrigin );
    render_view_trans = glm::translate( glm::mat4( 1.0f ), -1.0f * ( render_pos - renderOriginF ) );

    const glm::mat4 mvp_sky = globalGameState.screen.proj * render_view_look;
    // glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * globalGameState.camera.view_look;

    const glm::mat4 mvp = mvp_sky * render_view_trans;
    constexpr glm::mat4 rotation = glm::mat4( //
        1, 0, 0, 0,                           //
        0, -1, 0, 0,                          //
        0, 0, 1, 0,                           //
        0, 0, 0, 1                            //
    );

    const glm::mat4 flipped_look = render_view_look * rotation;
    // glm::mat4 flipped_look = rotation * globalGameState.camera.view_look;

    // glm::mat4 flipped_trans = rotation * globalGameState.camera.view_trans;
    // TODO globalGameState.camera.y causes a strange jump, perhaps the vars are updated in the wrong order.
    const float height_above_water = render_pos.y + ( 1.0f - WATER_HEIGHT );
    const float offset = 2.0f * height_above_water;
    const glm::mat4 flipped_trans = glm::translate( render_view_trans, glm::vec3( 0.0, offset, 0.0 ) );
    // glm::mat4 flipped_trans = globalGameState.camera.view_trans;

    const glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * flipped_look;

    const glm::mat4 mvp_reflect = globalGameState.screen.proj * flipped_look * flipped_trans;

    globalGameState.multiplayer.process_events( globalGameState.world );
    globalGameState.multiplayer.update_players_position( render_pos, render_rotation );

#if defined( REPGAME_WASM )
    // With pthreads enabled, terrain generation runs on background workers and
    // the main thread just drains finished chunks (like native). Without
    // pthreads, generation happens inline in dequeue(), so limit_render keeps
    // the time-budgeted loop in render_chunks from blocking the frame.
#  if defined( __EMSCRIPTEN_PTHREADS__ )
    const bool limit_render = false;
#  else
    const bool limit_render = true;
#  endif
    const bool do_render = true;
#else
    const bool do_render = true;
    const bool limit_render = false;
#endif
    if ( do_render ) {
        const long long t_render_start = now_us( );
        globalGameState.world.render( globalGameState.multiplayer, render_pos, limit_render, render_rotation );
        profiling.us_render = now_us( ) - t_render_start;
        profiling.num_drawable_chunks = globalGameState.world.get_num_drawable_chunks( );
        profiling.num_chunks_remeshed = globalGameState.world.get_num_remeshed_chunks( );
    }

    showErrors( );

    // glm::mat4 mvp_mirror = glm::translate( mvp, glm::vec3( 0, -10, 0 ) );
    // glm::mat4 rotation = glm::diagonal4x4( glm::vec4( 1, 1, -1, 1 ) );
    // glm::mat4 rotation = glm::rotate( glm::mat4( 1.0 ), glm::radians( 90.0f ), glm::vec3( 1, 0, 1 ) );
    // glm::mat4 mvp_reflect = mvp * rotation;
    // mvp_mirror = glm::translate( mvp_mirror, glm::vec3( 0, -10, 0 ) );
    glm::ivec3 round_block = glm::round( render_pos - 0.5f );
    BlockState blockInHead = globalGameState.world.get_loaded_block( round_block );
    bool headInWater = blockInHead.id == WATER;

    globalGameState.blocksTexture.bind( );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );

    {
        const long long t_world_draw_start = now_us( );
        globalGameState.world.draw( globalGameState.blocksTexture, mvp, mvp_reflect, mvp_sky, mvp_sky_reflect, globalGameState.input.debug_mode, !globalGameState.input.inventory_open, render_pos.y, headInWater,
                                    globalGameState.input.worldDrawQuality, render_pos, renderOrigin );
        profiling.us_world_draw = now_us( ) - t_world_draw_start;
    }

    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    showErrors( );
    // Clear depth and stencil before drawing the UI/ImGui overlays. The world
    // compositing pass uses stencil on the default framebuffer (for the
    // water-blur separation) and leaves stencil values behind; clearing it
    // here ensures a clean state for the overlays.
    glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

    {
        const long long t_ui_start = now_us( );
        globalGameState.ui_overlay.draw( globalGameState.main_inventory, globalGameState.hotbar, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.input, globalGameState.screen.ortho_center );
        ImGuiDebugVars &debugVars = imgui_overlay_get_imgui_debug_vars( );
        debugVars.player_pos = globalGameState.camera.pos;
        imgui_overlay_draw( &globalGameState.imgui_overlay, globalGameState.input );
        profiling.us_ui_draw = now_us( ) - t_ui_start;
    }
    showErrors( );

    if ( globalGameState.input.screenshot_requested ) {
        globalGameState.input.screenshot_requested = false;
        // Capture intermediate framebuffer attachments for debugging.
        std::string prefix = "screenshot_" + std::to_string( screenshot_counter++ );
        globalGameState.world.screenshot( prefix );
    }
    profiling.us_total_draw = now_us( ) - t_draw_start;
}

void RepGame::cleanup( ) {
    static bool clean_up_done = false;
    if ( clean_up_done ) {
        return;
    }

    PlayerData saved_data;
    saved_data.world_x = globalGameState.camera.pos.x;
    saved_data.world_y = globalGameState.camera.pos.y;
    saved_data.world_z = globalGameState.camera.pos.z;
    saved_data.angle_H = globalGameState.camera.angle_H;
    saved_data.angle_V = globalGameState.camera.angle_V;
    saved_data.flying = globalGameState.input.player_flying;
    saved_data.no_clip = globalGameState.input.no_clip;
    saved_data.worldDrawQuality = static_cast<int>( globalGameState.input.worldDrawQuality );
    globalGameState.hotbar.saveInventory( saved_data.hotbar_inventory );
    saved_data.selected_hotbar_slot = globalGameState.hotbar.getSelectedSlot( );

    globalGameState.map_storage.write_player_data( saved_data );
#if defined( REPGAME_WASM )
    EM_ASM( "FS.syncfs(false, err => {console.log(\"Sync done, its OK to close RepGame:\", err)});" );
#endif

    globalGameState.multiplayer.cleanup( );
    globalGameState.world.cleanup( globalGameState.map_storage );
    globalGameState.blocksTexture.destroy( );
    globalGameState.ui_overlay.cleanup( );
    globalGameState.main_inventory.cleanup( );
    globalGameState.hotbar.cleanup( );
    imgui_overlay_cleanup( &globalGameState.imgui_overlay );
    block_definitions_free_definitions( );

    clean_up_done = true;
    // pr_debug( "RepGame cleanup done" );
}

Input &RepGame::getInputState( ) {
    return globalGameState.input;
}