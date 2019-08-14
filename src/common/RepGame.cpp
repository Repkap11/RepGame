
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/utils/map_storage.hpp"
#include "common/abstract/textures.hpp"
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

RepGameState globalGameState;

void change_block( int place, BlockState blockState ) {
    TRIP_STATE( int block_ );
    Block *block = block_definition_get_definition( blockState.id );

    if ( place ) {
        block_x = globalGameState.block_selection.create_x;
        block_y = globalGameState.block_selection.create_y;
        block_z = globalGameState.block_selection.create_z;
        if ( render_order_collides_with_player( block_definition_get_definition( blockState.id )->renderOrder ) ) {
            // If the block collides with the player, make sure its not being placed where it would collide
            if ( collision_check_collides_with_block( &globalGameState.world, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z, TRIP_ARGS( block_ ) ) ) {
                return;
            }
        }
        if ( block->needs_place_on_solid ) {
            BlockID under_blockID = world_get_loaded_block( &globalGameState.world, block_x, block_y - 1, block_z ).id;
            Block *under_block = block_definition_get_definition( under_blockID );
            if ( !render_order_collides_with_player( under_block->renderOrder ) ) {
                return;
            }
        }

    } else {
        block_x = globalGameState.block_selection.destroy_x;
        block_y = globalGameState.block_selection.destroy_y;
        block_z = globalGameState.block_selection.destroy_z;
    }
    multiplayer_set_block( place, block_x, block_y, block_z, blockState );
    world_set_loaded_block( &globalGameState.world, TRIP_ARGS( block_ ), blockState );
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

void repgame_process_mouse_events( ) {
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle ) {
        BlockState blockState = world_get_loaded_block( &globalGameState.world, TRIP_ARGS( globalGameState.block_selection.destroy_ ) );
        pr_debug( "Selected block:%d", blockState.id );
        globalGameState.block_selection.holdingBlock = blockState.id;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left && globalGameState.input.click_delay_left == 0 ) {
        change_block( 0, {AIR, BLOCK_ROTATE_0} );
        globalGameState.input.click_delay_left = 8;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        unsigned char rotation = getPlacedRotation( globalGameState.block_selection.holdingBlock );
        change_block( 1, {globalGameState.block_selection.holdingBlock, rotation} );
        globalGameState.input.click_delay_right = 8;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.currentPosition.wheel_counts != globalGameState.input.mouse.previousPosition.wheel_counts ) {
        int wheel_diff = globalGameState.input.mouse.currentPosition.wheel_counts > globalGameState.input.mouse.previousPosition.wheel_counts ? 1 : -1;
        BlockState blockState = world_get_loaded_block( &globalGameState.world, TRIP_ARGS( globalGameState.block_selection.destroy_ ) );
        int blockID_int = ( int )blockState.id;
        do {
            blockID_int += wheel_diff;
            if ( blockID_int >= LAST_BLOCK_ID ) {
                blockID_int = LAST_BLOCK_ID - 1;
            }
            if ( blockID_int <= 0 ) {
                blockID_int = 1;
            }
        } while ( !render_order_is_pickable( block_definition_get_definition( ( BlockID )blockID_int )->renderOrder ) );
        blockState.id = ( BlockID )blockID_int;
        blockState.rotation = getPlacedRotation( blockState.id );
        change_block( 0, blockState );
    }
    globalGameState.input.mouse.currentPosition.wheel_counts = 0;
}

void repgame_process_camera_angle( ) {
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

    globalGameState.camera.mx = sin( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );
    globalGameState.camera.my = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    globalGameState.camera.mz = -cos( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );

    globalGameState.camera.view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                                                    globalGameState.camera.look,   // Look at look vector
                                                    glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
    );
    globalGameState.camera.view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -globalGameState.camera.x, -globalGameState.camera.y, -globalGameState.camera.z ) );
}
void repgame_process_movement( ) {
    float movement_vector_x = MOVEMENT_SENSITIVITY * globalGameState.input.movement.sizeH * globalGameState.camera.mx;
    float movement_vector_y = MOVEMENT_SENSITIVITY * globalGameState.input.movement.sizeV;
    float movement_vector_z = MOVEMENT_SENSITIVITY * globalGameState.input.movement.sizeH * globalGameState.camera.mz;

    if ( movement_vector_y == 0 ) {
        movement_vector_y = -GRAVITY_STRENGTH;
    }

    collision_check_move( &globalGameState.world, TRIP_ARGS( &movement_vector_ ), //
                          globalGameState.camera.x,                               //
                          globalGameState.camera.y,                               //
                          globalGameState.camera.z );

    globalGameState.camera.x += movement_vector_x;
    globalGameState.camera.y += movement_vector_y;
    globalGameState.camera.z += movement_vector_z;
}
void repgame_idle( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother being idle if the state if the game is exiting
        return;
    }
    multiplayer_process_events( &globalGameState.world );
    world_render( &globalGameState.world, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z, 1, globalGameState.camera.rotation );
}

void repgame_tick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }

    if ( repgame_should_lock_pointer( ) ) {
        repgame_process_mouse_events( );
        int whichFace = 0;
        globalGameState.block_selection.selectionInBounds = ray_traversal_find_block_from_to(
            &globalGameState.world, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z, globalGameState.camera.x + globalGameState.camera.look.x * REACH_DISTANCE,
            globalGameState.camera.y + globalGameState.camera.look.y * REACH_DISTANCE, globalGameState.camera.z + globalGameState.camera.look.z * REACH_DISTANCE, TRIP_ARGS( &globalGameState.block_selection.destroy_ ), &whichFace, 0, 1 );

        globalGameState.block_selection.create_x = globalGameState.block_selection.destroy_x + ( whichFace == FACE_RIGHT ) - ( whichFace == FACE_LEFT );
        globalGameState.block_selection.create_y = globalGameState.block_selection.destroy_y + ( whichFace == FACE_TOP ) - ( whichFace == FACE_BOTTOM );
        globalGameState.block_selection.create_z = globalGameState.block_selection.destroy_z + ( whichFace == FACE_BACK ) - ( whichFace == FACE_FRONT );

        world_set_selected_block( &globalGameState.world, TRIP_ARGS( globalGameState.block_selection.destroy_ ), globalGameState.block_selection.selectionInBounds );
        globalGameState.camera.angle_H += ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x ) * 0.04f;
        globalGameState.camera.angle_V += ( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) * 0.04f;
        if ( globalGameState.camera.angle_H >= 360.0f ) {
            globalGameState.camera.angle_H -= 360.0f;
        }
        if ( globalGameState.camera.angle_H < 0.0f ) {
            globalGameState.camera.angle_H += 360.0f;
        }
    }
    globalGameState.input.mouse.currentPosition.x = globalGameState.screen.width / 2;
    globalGameState.input.mouse.currentPosition.y = globalGameState.screen.height / 2;

    repgame_process_camera_angle( );
    repgame_process_movement( );

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
}

static inline void initilizeGameState( const char *world_name ) {
    globalGameState.input.exitGame = 0;
    globalGameState.input.limit_fps = 1;
    globalGameState.camera.angle_H = 135.0f;
    globalGameState.camera.angle_V = 25.0f;
    globalGameState.camera.x = 0.0f;
    globalGameState.camera.y = 8.5f;
    globalGameState.camera.z = 0.0f;
    globalGameState.block_selection.holdingBlock = GRASS;

    map_storage_init( world_name );
    PlayerData saved_data;
    int has_saved_data = map_storage_read_player_data( &saved_data );
    if ( has_saved_data ) {
        globalGameState.camera.x = saved_data.world_x;
        globalGameState.camera.y = saved_data.world_y;
        globalGameState.camera.z = saved_data.world_z;
        globalGameState.block_selection.holdingBlock = saved_data.holdingBlock;
        globalGameState.camera.angle_H = saved_data.angle_H;
        globalGameState.camera.angle_V = saved_data.angle_V;
    }
}

int check_block( Block *block ) {
    if ( block != NULL ) {
        BlockID blockID = block->id;
        if ( !( blockID == AIR || blockID == AIR ) ) {
            return 1;
        }
    } else {
        // pr_debug( "No Block!!" );
    }
    return 0;
}

MK_TEXTURE( textures, 384, 816, 16, 16, 138 );

void repgame_init( const char *world_name ) {
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBlendEquation( GL_FUNC_ADD );

    initilizeGameState( world_name );
    texture_init( &globalGameState.blocksTexture, &texture_source_textures );
    block_definitions_initilize_definitions( &globalGameState.blocksTexture );
    world_init( &globalGameState.world, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );
    ui_overlay_init( &globalGameState.ui_overlay );

    int iMultiSample = 0;
    int iNumSamples = 0;
    glGetIntegerv( GL_SAMPLE_BUFFERS, &iMultiSample );
    glGetIntegerv( GL_SAMPLES, &iNumSamples );
    // pr_debug( "GL_SAMPLE_BUFFERS = %d, GL_SAMPLES = %d", iMultiSample, iNumSamples );
}

void repgame_set_textures( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    textures_set_texture_data( which_texture, textures, textures_len );
}

int repgame_shouldExit( ) {
    return globalGameState.input.exitGame;
}

int repgame_should_lock_pointer( ) {
    return !globalGameState.input.inventory_open;
}
void repgame_changeSize( int w, int h ) {
    // pr_debug( "Screen Size Change:%dx%d", w, h );
    if ( w == 0 || h == 0 ) {
        return;
    }
    globalGameState.screen.width = w;
    globalGameState.screen.height = h;

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;

    glViewport( 0, 0, w, h );
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, 800.0f );
    globalGameState.screen.ortho = glm::ortho<float>( 0.f, w, 0.f, h, -1.f, 1.f );
    globalGameState.screen.ortho_center = glm::ortho<float>( -w / 2, w / 2, -h / 2, h / 2, -1.f, 1.f );
}

void repgame_clear( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}
void repgame_get_screen_size( int *width, int *height ) {
    *width = globalGameState.screen.width;
    *height = globalGameState.screen.height;
}

// int should_upate_count = 0;
void repgame_draw( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother draw the state if the game is exiting
        return;
    }
    glm::mat4 mvp_sky = globalGameState.screen.proj * globalGameState.camera.view_look;
    //glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * globalGameState.camera.view_look;

    glm::mat4 mvp = mvp_sky * globalGameState.camera.view_trans;
    glm::mat4 rotation = glm::diagonal4x4( glm::vec4( 1, -1, 1, 1 ) );

    glm::mat4 flipped_look = globalGameState.camera.view_look * rotation;
    glm::mat4 flipped_trans = glm::translate( globalGameState.camera.view_trans, glm::vec3( 0.0, 1.475, 0.0 ) );
    glm::mat4 mvp_reflect = globalGameState.screen.proj * flipped_look * flipped_trans;
#if defined( REPGAME_WASM )
#else
    multiplayer_process_events( &globalGameState.world );
    // if ( should_upate_count > 100 ) {
    multiplayer_update_players_position( globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z, globalGameState.camera.rotation );
    //    should_upate_count = 0;
    //}
    // should_upate_count++;
    world_render( &globalGameState.world, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z, false, globalGameState.camera.rotation );
#endif
    showErrors( );

    // glm::mat4 mvp_mirror = glm::translate( mvp, glm::vec3( 0, -10, 0 ) );
    // glm::mat4 rotation = glm::diagonal4x4( glm::vec4( 1, 1, -1, 1 ) );
    // glm::mat4 rotation = glm::rotate( glm::mat4( 1.0 ), glm::radians( 90.0f ), glm::vec3( 1, 0, 1 ) );
    // glm::mat4 mvp_reflect = mvp * rotation;
    // mvp_mirror = glm::translate( mvp_mirror, glm::vec3( 0, -10, 0 ) );

    world_draw( &globalGameState.world, &globalGameState.blocksTexture, mvp, mvp_reflect, mvp_sky, globalGameState.input.debug_mode, !globalGameState.input.inventory_open );
    ui_overlay_draw( &globalGameState.ui_overlay, &globalGameState.world.renderer, &globalGameState.blocksTexture, &globalGameState.input, globalGameState.screen.ortho_center );
}

void repgame_cleanup( ) {
    static int clean_up_done = 0;
    if ( clean_up_done ) {
        return;
    }
    clean_up_done = 1;
    world_cleanup( &globalGameState.world );
    texture_destroy( &globalGameState.blocksTexture );
    block_definitions_free_definitions( );

    PlayerData saved_data;
    saved_data.world_x = globalGameState.camera.x;
    saved_data.world_y = globalGameState.camera.y;
    saved_data.world_z = globalGameState.camera.z;
    saved_data.angle_H = globalGameState.camera.angle_H;
    saved_data.angle_V = globalGameState.camera.angle_V;

    saved_data.holdingBlock = globalGameState.block_selection.holdingBlock;
    map_storage_write_player_data( &saved_data );
#if defined( REPGAME_WASM )
    EM_ASM( "FS.syncfs(false, err => {console.log(\"Sync done, its OK to close RepGame:\", err)});" );
#endif
    // pr_debug( "RepGame cleanup done" );
}

InputState *repgame_getInputState( ) {
    return &globalGameState.input;
}