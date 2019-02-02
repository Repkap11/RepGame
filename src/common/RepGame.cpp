
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
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

RepGameState globalGameState;

void change_block( int place, BlockID blockID ) {
    TRIP_STATE( int block_ );
    if ( place ) {
        block_x = globalGameState.block_selection.create_x;
        block_y = globalGameState.block_selection.create_y;
        block_z = globalGameState.block_selection.create_z;
    } else {
        block_x = globalGameState.block_selection.destroy_x;
        block_y = globalGameState.block_selection.destroy_y;
        block_z = globalGameState.block_selection.destroy_z;
    }
    world_set_block( &globalGameState.gameChunks, TRIP_ARGS( block_ ), blockID );
}
static void gameTick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }

    int whichFace = 0;
    globalGameState.block_selection.selectionInBounds =                                                                 //
        ray_traversal_find_block_from_to( &globalGameState.gameChunks,                                                  //
                                          globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z, //
                                          globalGameState.camera.x + globalGameState.camera.look.x * REACH_DISTANCE,    //
                                          globalGameState.camera.y + globalGameState.camera.look.y * REACH_DISTANCE,    //
                                          globalGameState.camera.z + globalGameState.camera.look.z * REACH_DISTANCE,    //
                                          TRIP_ARGS( &globalGameState.block_selection.destroy_ ), &whichFace, 0 );
    globalGameState.block_selection.create_x = globalGameState.block_selection.destroy_x + ( whichFace == FACE_RIGHT ) - ( whichFace == FACE_LEFT );
    globalGameState.block_selection.create_y = globalGameState.block_selection.destroy_y + ( whichFace == FACE_TOP ) - ( whichFace == FACE_BOTTOM );
    globalGameState.block_selection.create_z = globalGameState.block_selection.destroy_z + ( whichFace == FACE_BACK ) - ( whichFace == FACE_FRONT );

    chunk_loader_set_selected_block( &globalGameState.gameChunks, TRIP_ARGS( globalGameState.block_selection.destroy_ ), globalGameState.block_selection.selectionInBounds );

    globalGameState.block_selection.selectionInBounds = sqrt(                                                                                                                                                     //
                                                            ( globalGameState.camera.x - globalGameState.block_selection.destroy_x ) * ( globalGameState.camera.x - globalGameState.block_selection.destroy_x ) + //
                                                            ( globalGameState.camera.y - globalGameState.block_selection.destroy_y ) * ( globalGameState.camera.y - globalGameState.block_selection.destroy_y ) + //
                                                            ( globalGameState.camera.z - globalGameState.block_selection.destroy_z ) * ( globalGameState.camera.z - globalGameState.block_selection.destroy_z ) ) < REACH_DISTANCE;

    // int block_x = globalGameState.block_selection.destroy_x;
    // int block_y = globalGameState.block_selection.destroy_y;
    // int block_z = globalGameState.block_selection.destroy_z;

    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle ) {
        BlockID blockID = world_get_loaded_block( &globalGameState.gameChunks, TRIP_ARGS( globalGameState.block_selection.destroy_ ) );
        globalGameState.block_selection.holdingBlock = blockID;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left && globalGameState.input.click_delay_left == 0 ) {
        change_block( 0, AIR );
        globalGameState.input.click_delay_left = 16;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        change_block( 1, globalGameState.block_selection.holdingBlock );
        globalGameState.input.click_delay_right = 8;
    }
    if ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x || globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) {
        // pr_debug( "Position Diff:%d %d", input.mouse.currentPosition.x - input.mouse.previousPosition.x, input.mouse.currentPosition.y - input.mouse.previousPosition.y );
    }

    globalGameState.camera.angle_H += ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x ) * 0.04f;
    globalGameState.camera.angle_V += ( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) * 0.04f;
    globalGameState.input.mouse.currentPosition.x = globalGameState.screen.width / 2;
    globalGameState.input.mouse.currentPosition.y = globalGameState.screen.height / 2;
    // pr_debug( "Angle_V:%f", globalGameState.camera.angle_V );
    // Bpr_debug( "Angle_H:%f", globalGameState.camera.angle_H );

    float upAngleLimit = 90 - 0.001f;
    if ( globalGameState.camera.angle_V > upAngleLimit ) {
        globalGameState.camera.angle_V = upAngleLimit;
    }

    float downAngleLimit = -90 + 0.001f;
    if ( globalGameState.camera.angle_V < downAngleLimit ) {
        globalGameState.camera.angle_V = downAngleLimit;
    }

    // Only the first time, calculate the chunk
    if ( globalGameState.physics.chunk == NULL ) {
        // pr_debug( "Doing initial search" );
        globalGameState.physics.chunk = world_get_loaded_chunk( &globalGameState.gameChunks, globalGameState.camera.x, globalGameState.camera.y - EYE_POSITION_OFFSET, globalGameState.camera.z );
    }
    if ( globalGameState.physics.chunk != NULL ) {
        int chunk_x = floor( ( int )globalGameState.camera.x / ( float )CHUNK_SIZE );
        int chunk_y = floor( ( int )( globalGameState.camera.y - EYE_POSITION_OFFSET ) / ( float )CHUNK_SIZE );
        int chunk_z = floor( ( int )globalGameState.camera.z / ( float )CHUNK_SIZE );

        int chunk_changed_x = chunk_x != globalGameState.physics.chunk->chunk_x;
        int chunk_changed_y = chunk_y != globalGameState.physics.chunk->chunk_y;
        int chunk_changed_z = chunk_z != globalGameState.physics.chunk->chunk_z;

        int chunk_changed = TRIP_OR( chunk_changed_ );

        // if ( chunk_changed ) {
        //     // pr_debug( "Searching for chunk" );
        //     globalGameState.physics.chunk = world_get_loaded_chunk( &globalGameState.gameChunks, globalGameState.camera.x, globalGameState.camera.y - EYE_POSITION_OFFSET, globalGameState.camera.z );
        //     if ( globalGameState.physics.chunk == NULL ) {
        //         pr_debug( "Chunk not found for physics...." );
        //     }
        // }

        globalGameState.physics.diff_x = ( float )globalGameState.camera.x - globalGameState.physics.chunk->chunk_x * CHUNK_SIZE;
        globalGameState.physics.diff_y = ( float )( globalGameState.camera.y - EYE_POSITION_OFFSET ) - globalGameState.physics.chunk->chunk_y * CHUNK_SIZE;
        globalGameState.physics.diff_z = ( float )globalGameState.camera.z - globalGameState.physics.chunk->chunk_z * CHUNK_SIZE;
    }

    globalGameState.input.mouse.previousPosition.x = globalGameState.screen.width / 2;
    globalGameState.input.mouse.previousPosition.y = globalGameState.screen.height / 2;

    globalGameState.camera.look = glm::normalize( glm::vec3(        //
        sin( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) ), //
        -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) ),    //
        -cos( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) ) ) );

    globalGameState.camera.mx = sin( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );
    globalGameState.camera.my = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    globalGameState.camera.mz = -cos( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );

    globalGameState.camera.view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                                                    globalGameState.camera.look,   // Look at look vector
                                                    glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
    );
    globalGameState.camera.view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -globalGameState.camera.x,     //
                                                                                      -globalGameState.camera.y,     //
                                                                                      -globalGameState.camera.z ) ); //

    float movement_vector_x = globalGameState.input.movement.sizeH * globalGameState.camera.mx;
    float movement_vector_y = globalGameState.input.movement.sizeV;
    float movement_vector_z = globalGameState.input.movement.sizeH * globalGameState.camera.mz;

    movement_vector_x *= MOVEMENT_SENSITIVITY;
    movement_vector_y *= MOVEMENT_SENSITIVITY;
    movement_vector_z *= MOVEMENT_SENSITIVITY;

    collision_check_move( &globalGameState.gameChunks, TRIP_ARGS( &movement_vector_ ), //
                          globalGameState.camera.x,                                    //
                          globalGameState.camera.y,                                    //
                          globalGameState.camera.z );

    globalGameState.camera.x += movement_vector_x;
    globalGameState.camera.y += movement_vector_y;
    globalGameState.camera.z += movement_vector_z;

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
}

static inline void initilizeGameState( ) {
    globalGameState.input.exitGame = 0;
    globalGameState.input.limit_fps = 1;
    globalGameState.camera.angle_H = 135.0f;
    globalGameState.camera.angle_V = 25.0f;
    globalGameState.camera.x = -1.0f;
    globalGameState.camera.y = 10.0f;
    globalGameState.camera.z = -1.0f;
    globalGameState.block_selection.holdingBlock = TNT;
    world_init( &globalGameState.gameChunks );
    ui_overlay_init( &globalGameState.ui_overlay );
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

void repgame_init( ) {
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBlendEquation( GL_FUNC_ADD );

    initilizeGameState( );
    int iMultiSample = 0;
    int iNumSamples = 0;
    glGetIntegerv( GL_SAMPLE_BUFFERS, &iMultiSample );
    glGetIntegerv( GL_SAMPLES, &iNumSamples );
    pr_debug( "GL_SAMPLE_BUFFERS = %d, GL_SAMPLES = %d", iMultiSample, iNumSamples );
}

void repgame_set_textures( unsigned int which_texture, unsigned char *textures, int textures_len ) {
    textures_set_texture_data( which_texture, textures, textures_len );
}

int repgame_shouldExit( ) {
    return globalGameState.input.exitGame;
}

void repgame_changeSize( int w, int h ) {
    pr_debug( "Screen Size Change:%dx%d", w, h );
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

void repgame_tick( ) {
    gameTick( );
}

void repgame_clear( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}
void repgame_get_screen_size( int *width, int *height ) {
    *width = globalGameState.screen.width;
    *height = globalGameState.screen.height;
}

void repgame_draw( ) {
    glm::mat4 mvp_sky = globalGameState.screen.proj * globalGameState.camera.view_look;
    glm::mat4 mvp = mvp_sky * globalGameState.camera.view_trans;
    pr_debug( "World about to render" );
    world_render( &globalGameState.gameChunks, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );
    pr_debug( "World Rendered" );
    showErrors( );

    world_draw_solid( &globalGameState.gameChunks, mvp, mvp_sky );
    world_draw_liquid( &globalGameState.gameChunks, mvp );
    chunk_loader_draw_mouse_selection( &globalGameState.gameChunks );
    ui_overlay_draw( &globalGameState.ui_overlay, &globalGameState.gameChunks.renderer, globalGameState.screen.ortho_center );
}

void repgame_cleanup( ) {
    world_cleanup( &globalGameState.gameChunks );
    block_definitions_free_definitions( );
    // pr_debug( "RepGame cleanup done" );
}

InputState *repgame_getInputState( ) {
    return &globalGameState.input;
}