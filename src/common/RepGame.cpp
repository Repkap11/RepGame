
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/utils/map_storage.hpp"
#include "common/abstract/texture.hpp"
#include "common/world.hpp"
#include "common/abstract/shader.hpp"
#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/index_buffer.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"
#include "common/abstract/vertex_array.hpp"
#include "common/abstract/renderer.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"

RepGameState globalGameState;

BlockID change_block( int place, BlockState blockState ) {

    glm::ivec3 block;
    if ( place ) {
        block = globalGameState.block_selection.pos_create;
        if ( block_definition_get_definition( blockState.id )->collides_with_player ) {
            // If the block collides with the player, make sure its not being placed where it would collide
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

unsigned char getPlacedRotation( BlockID blockID ) {
    unsigned char rotation = BLOCK_ROTATE_0;
    if ( block_definition_get_definition( blockID )->rotate_on_placement ) {
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

void RepGame::add_to_an_inventory( BlockID blockId ) {
    bool didAdd = globalGameState.hotbar.addBlock( blockId );
    if ( didAdd ) {
        globalGameState.block_selection.holdingBlock = blockId;
        globalGameState.ui_overlay.set_holding_block( globalGameState.block_selection.holdingBlock );
    } else {
        globalGameState.inventory.addBlock( blockId );
    }
}

static bool was_middle = false;
void RepGame::process_mouse_events( ) {
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle ) {
        BlockState blockState = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        RepGame::add_to_an_inventory( blockState.id );
        if ( !was_middle ) {
            pr_debug( "Selected block:%d rotation:%d redstone_power:%d display:%d", blockState.id, blockState.rotation, blockState.current_redstone_power, blockState.display_id );
        }
        was_middle = true;
    } else {
        was_middle = false;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left && globalGameState.input.click_delay_left == 0 ) {
        // Mining a block
        BlockID previous_block = change_block( 0, BLOCK_STATE_AIR );
        RepGame::add_to_an_inventory( previous_block );
        globalGameState.input.click_delay_left = 30;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        // Placeing a block
        if ( globalGameState.block_selection.holdingBlock != LAST_BLOCK_ID ) {
            unsigned char rotation = getPlacedRotation( globalGameState.block_selection.holdingBlock );
            BlockID previous_block = change_block( 1, { globalGameState.block_selection.holdingBlock, rotation, 0, globalGameState.block_selection.holdingBlock } );
            globalGameState.input.click_delay_right = 30;
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

void RepGame::process_camera_angle( ) {
    float upAngleLimit = 90 - 0.001f;
    if ( globalGameState.camera.angle_V > upAngleLimit ) {
        globalGameState.camera.angle_V = upAngleLimit;
    }
    float downAngleLimit = -90 + 0.001f;
    if ( globalGameState.camera.angle_V < downAngleLimit ) {
        globalGameState.camera.angle_V = downAngleLimit;
    }
    globalGameState.camera.look = glm::normalize( glm::vec3(        //
        sin( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) ), //
        -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) ),    //
        -cos( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) ) ) );

    glm::mat4 rotate = glm::rotate( glm::mat4( 1.0f ), glm::radians( -globalGameState.camera.angle_H + 180 ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    rotate = glm::rotate( rotate, glm::radians( globalGameState.camera.angle_V ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    globalGameState.camera.rotation = rotate;

    globalGameState.camera.movement.x = sin( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );
    globalGameState.camera.movement.y = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    globalGameState.camera.movement.z = -cos( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );

    globalGameState.camera.view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                                                    globalGameState.camera.look,   // Look at look vector
                                                    glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
    );
    globalGameState.camera.view_trans = glm::translate( glm::mat4( 1.0f ), -1.0f * globalGameState.camera.pos );
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
        if ( globalGameState.input.movement.sneakPressed && globalGameState.input.movement.jumpPressed ) {
            globalGameState.camera.y_speed = 0;
        } else {
            if ( globalGameState.input.movement.sneakPressed ) {
                globalGameState.camera.y_speed = -1.0f * ( JUMP_STRENGTH + TERMINAL_VELOCITY ) / 4.0f;
            } else if ( globalGameState.input.movement.jumpPressed ) {
                globalGameState.camera.y_speed = ( JUMP_STRENGTH + TERMINAL_VELOCITY ) / 4.0f;
                ;
            } else {
                globalGameState.camera.y_speed = 0;
            }
        }
    }

    float accel = -gravity;

    glm::vec3 movement_vector = glm::vec3( );
    movement_vector.x = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.x;
    movement_vector.y = globalGameState.camera.y_speed + accel;
    if ( movement_vector.y > TERMINAL_VELOCITY ) {
        movement_vector.y = TERMINAL_VELOCITY;
    }
    if ( movement_vector.y < -TERMINAL_VELOCITY ) {
        movement_vector.y = -TERMINAL_VELOCITY;
    }
    movement_vector.z = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.z;
    if ( globalGameState.input.no_clip ) {
        globalGameState.camera.standing_on_solid = 0;
    } else {
        Collision::check_move( globalGameState.world, movement_vector, //
                               globalGameState.camera.pos,             //
                               &globalGameState.camera.standing_on_solid );
    }

    globalGameState.camera.pos = globalGameState.camera.pos + movement_vector;
    globalGameState.camera.y_speed = movement_vector.y;
    // pr_debug("Y speed:%f",globalGameState.camera.y_speed);
}

void RepGame::process_block_updates( ) {
    // for ( size_t i = 0; i < 20; i++ ) {
    //     world_process_random_ticks( &globalGameState.world );
    // }
    globalGameState.blockUpdateQueue.processAllBlockUpdates( globalGameState.world, globalGameState.tick_number );
}

void RepGame::tick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    globalGameState.tick_number++;

    globalGameState.multiplayer.process_events( globalGameState.world );

    int wheel_diff = globalGameState.input.mouse.previousPosition.wheel_counts - globalGameState.input.mouse.currentPosition.wheel_counts;
    if ( wheel_diff != 0 ) {
        globalGameState.block_selection.holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
        globalGameState.ui_overlay.set_holding_block( globalGameState.block_selection.holdingBlock );
    }

    if ( RepGame::should_lock_pointer( ) ) {
        RepGame::process_mouse_events( );
        int whichFace = 0;
        glm::vec3 pos_with_reach = globalGameState.camera.pos + ( globalGameState.camera.look * ( ( float )REACH_DISTANCE ) );
        globalGameState.block_selection.selectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, NULL, globalGameState.camera.pos, pos_with_reach, globalGameState.block_selection.pos_destroy, &whichFace, 0, 1, 0 );

        globalGameState.block_selection.pos_create.x = globalGameState.block_selection.pos_destroy.x + ( whichFace == FACE_RIGHT ) - ( whichFace == FACE_LEFT );
        globalGameState.block_selection.pos_create.y = globalGameState.block_selection.pos_destroy.y + ( whichFace == FACE_TOP ) - ( whichFace == FACE_BOTTOM );
        globalGameState.block_selection.pos_create.z = globalGameState.block_selection.pos_destroy.z + ( whichFace == FACE_BACK ) - ( whichFace == FACE_FRONT );

        globalGameState.world.set_selected_block( globalGameState.block_selection.pos_destroy, globalGameState.block_selection.selectionInBounds );
        globalGameState.camera.angle_H += ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x ) * 0.08f;
        globalGameState.camera.angle_V += ( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) * 0.08f;
        if ( globalGameState.camera.angle_H >= 360.0f ) {
            globalGameState.camera.angle_H -= 360.0f;
        }
        if ( globalGameState.camera.angle_H < 0.0f ) {
            globalGameState.camera.angle_H += 360.0f;
        }
    }
    globalGameState.input.mouse.currentPosition.x = globalGameState.screen.width / 2;
    globalGameState.input.mouse.currentPosition.y = globalGameState.screen.height / 2;

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

    RepGame::process_block_updates( );
}

static inline void initilizeGameState( const char *world_name ) {
    globalGameState.input.exitGame = 0;
    globalGameState.input.player_flying = 0;
    globalGameState.input.inventory_open = 0;
    globalGameState.camera.angle_H = 135.0f;
    globalGameState.camera.angle_V = 25.0f;
    globalGameState.camera.pos.x = 0.0f;
    globalGameState.camera.pos.y = 8.5f;
    globalGameState.camera.pos.z = 0.0f;
    globalGameState.block_selection.holdingBlock = GRASS;
    globalGameState.inventory.inventory_renderer.options.active_height_percent = 0.5f;
    globalGameState.inventory.inventory_renderer.options.max_height_percent = 0.75f;
    globalGameState.inventory.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.inventory.inventory_renderer.options.gravity_bottom = false;
    globalGameState.inventory.inventory_renderer.options.shows_selection_slot = false;

    globalGameState.hotbar.inventory_renderer.options.active_height_percent = 1.0f;
    globalGameState.hotbar.inventory_renderer.options.max_height_percent = 0.1f;
    globalGameState.hotbar.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.hotbar.inventory_renderer.options.gravity_bottom = true;
    globalGameState.hotbar.inventory_renderer.options.shows_selection_slot = true;

    MapStorage::init( world_name );
    PlayerData saved_data;
    int has_saved_data = MapStorage::read_player_data( &saved_data );
    if ( has_saved_data ) {
        globalGameState.camera.pos.x = saved_data.world_x;
        globalGameState.camera.pos.y = saved_data.world_y;
        globalGameState.camera.pos.z = saved_data.world_z;
        globalGameState.block_selection.holdingBlock = saved_data.holdingBlock;
        globalGameState.block_selection.holdingBlock = BlockID::PODZEL;
        globalGameState.camera.angle_H = saved_data.angle_H;
        globalGameState.camera.angle_V = saved_data.angle_V;
        globalGameState.input.player_flying = saved_data.flying;
        globalGameState.input.no_clip = saved_data.no_clip;
        globalGameState.input.reflections_on = saved_data.reflections_on;
    }
}

MK_TEXTURE( textures, 384, 832, 16, 16 );

static bool globalSupportsAnisotropic;

bool RepGame::supportsAnisotropic( ) {
    return globalSupportsAnisotropic;
}

RepGameState *RepGame::init( const char *world_name, bool connect_multi, const char *host, bool supportsAnisotropicFiltering ) {
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

    initilizeGameState( world_name );
    globalGameState.blocksTexture.init( texture_source_textures, 0 );
    block_definitions_initilize_definitions( &globalGameState.blocksTexture );
    globalGameState.world.init( globalGameState.camera.pos, globalGameState.screen.width, globalGameState.screen.height );

    globalGameState.ui_overlay.init( &globalGameState.inventory, &globalGameState.hotbar );
    globalGameState.hotbar.setSelectedSlot( 0 );
    imgui_overlay_init( &globalGameState.imgui_overlay );

    globalGameState.ui_overlay.set_holding_block( globalGameState.block_selection.holdingBlock );

    int iMultiSample = 0;
    int iNumSamples = 0;
    glGetIntegerv( GL_SAMPLE_BUFFERS, &iMultiSample );
    glGetIntegerv( GL_SAMPLES, &iNumSamples );
    pr_debug( "GL_SAMPLE_BUFFERS = %d, GL_SAMPLES = %d", iMultiSample, iNumSamples );
    return &globalGameState;
}

void RepGame::set_textures( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    Texture::set_texture_data( which_texture, textures, textures_len );
}

int RepGame::shouldExit( ) {
    return globalGameState.input.exitGame;
}

int RepGame::should_lock_pointer( ) {
    return !globalGameState.input.inventory_open;
}
void RepGame::changeSize( int w, int h ) {
    // pr_debug( "Screen Size Change:%dx%d", w, h );
    if ( w == 0 || h == 0 ) {
        return;
    }
    globalGameState.screen.width = w;
    globalGameState.screen.height = h;
    glViewport( 0, 0, w, h );

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;
    globalGameState.ui_overlay.on_screen_size_change( w, h );
    globalGameState.inventory.onScreenSizeChange( w, h );
    globalGameState.hotbar.onScreenSizeChange( w, h );
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, 800.0f );
    globalGameState.screen.ortho = glm::ortho<float>( 0.f, w, 0.f, h, -1.f, 1.f );
    globalGameState.screen.ortho_center = glm::ortho<float>( -w / 2, w / 2, -h / 2, h / 2, -1.f, 1.f );
    globalGameState.world.change_size( w, h );
}

void RepGame::clear( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}
void RepGame::get_screen_size( int *width, int *height ) {
    *width = globalGameState.screen.width;
    *height = globalGameState.screen.height;
}

// int should_upate_count = 0;
void RepGame::draw( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother draw the state if the game is exiting
        return;
    }
    glm::mat4 mvp_sky = globalGameState.screen.proj * globalGameState.camera.view_look;
    // glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * globalGameState.camera.view_look;

    glm::mat4 mvp = mvp_sky * globalGameState.camera.view_trans;
    glm::mat4 rotation = glm::mat4( //
        1, 0, 0, 0,                 //
        0, -1, 0, 0,                //
        0, 0, 1, 0,                 //
        0, 0, 0, 1                  //
    );

    glm::mat4 flipped_look = globalGameState.camera.view_look * rotation;
    // glm::mat4 flipped_look = rotation * globalGameState.camera.view_look;

    // glm::mat4 flipped_trans = rotation * globalGameState.camera.view_trans;
    // TODO globalGameState.camera.y causes a strange jump, perhaps the vars are updated in the wrong order.
    float height_above_water = globalGameState.camera.pos.y + ( 1.0 - WATER_HEIGHT );
    float offset = 2.0f * height_above_water;
    glm::mat4 flipped_trans = glm::translate( globalGameState.camera.view_trans, glm::vec3( 0.0, offset, 0.0 ) );
    // glm::mat4 flipped_trans = globalGameState.camera.view_trans;

    glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * flipped_look;

    glm::mat4 mvp_reflect = globalGameState.screen.proj * flipped_look * flipped_trans;

    globalGameState.multiplayer.process_events( globalGameState.world );
    globalGameState.multiplayer.update_players_position( globalGameState.camera.pos, globalGameState.camera.rotation );

#if defined( REPGAME_WASM )
    bool limit_render = 1;
    bool do_render;
    static int should_update_count = 0;
    if ( should_update_count > 20 ) {
        should_update_count = 0;
        do_render = 1;
    } else {
        do_render = 0;
    }
    should_update_count++;
#else
    bool do_render = 1;
    bool limit_render = 0;
#endif
    if ( do_render ) {
        globalGameState.world.render( globalGameState.camera.pos, limit_render, globalGameState.camera.rotation );
    }

    showErrors( );

    // glm::mat4 mvp_mirror = glm::translate( mvp, glm::vec3( 0, -10, 0 ) );
    // glm::mat4 rotation = glm::diagonal4x4( glm::vec4( 1, 1, -1, 1 ) );
    // glm::mat4 rotation = glm::rotate( glm::mat4( 1.0 ), glm::radians( 90.0f ), glm::vec3( 1, 0, 1 ) );
    // glm::mat4 mvp_reflect = mvp * rotation;
    // mvp_mirror = glm::translate( mvp_mirror, glm::vec3( 0, -10, 0 ) );
    glm::ivec3 round_block = glm::round( globalGameState.camera.pos - 0.5f );
    BlockState blockInHead = globalGameState.world.get_loaded_block( round_block );
    bool headInWater = blockInHead.id == WATER;

    globalGameState.blocksTexture.bind( );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );

    globalGameState.world.draw( globalGameState.blocksTexture, mvp, mvp_reflect, mvp_sky, mvp_sky_reflect, globalGameState.input.debug_mode, !globalGameState.input.inventory_open, globalGameState.camera.pos.y, headInWater,
                                globalGameState.input.reflections_on );

    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    showErrors( );
    glClear( GL_DEPTH_BUFFER_BIT );
    globalGameState.ui_overlay.draw( globalGameState.world.renderer, globalGameState.blocksTexture, &globalGameState.input, globalGameState.screen.ortho_center );
    imgui_overlay_draw( &globalGameState.imgui_overlay, &globalGameState.input );
    showErrors( );
}

void RepGame::cleanup( ) {
    static int clean_up_done = 0;
    if ( clean_up_done ) {
        return;
    }
    clean_up_done = 1;
    globalGameState.multiplayer.cleanup( );
    globalGameState.world.cleanup( );
    globalGameState.blocksTexture.destroy( );
    globalGameState.ui_overlay.cleanup( );
    globalGameState.inventory.cleanup( );
    globalGameState.hotbar.cleanup( );
    imgui_overlay_cleanup( &globalGameState.imgui_overlay );
    block_definitions_free_definitions( );

    PlayerData saved_data;
    saved_data.world_x = globalGameState.camera.pos.x;
    saved_data.world_y = globalGameState.camera.pos.y;
    saved_data.world_z = globalGameState.camera.pos.z;
    saved_data.angle_H = globalGameState.camera.angle_H;
    saved_data.angle_V = globalGameState.camera.angle_V;
    saved_data.flying = globalGameState.input.player_flying;
    saved_data.no_clip = globalGameState.input.no_clip;
    saved_data.reflections_on = globalGameState.input.reflections_on;

    saved_data.holdingBlock = globalGameState.block_selection.holdingBlock;
    MapStorage::write_player_data( &saved_data );
#if defined( REPGAME_WASM )
    EM_ASM( "FS.syncfs(false, err => {console.log(\"Sync done, its OK to close RepGame:\", err)});" );
#endif
    // pr_debug( "RepGame cleanup done" );
}

InputState *RepGame::getInputState( ) {
    return &globalGameState.input;
}