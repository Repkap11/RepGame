
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "RepGame.h"
#include "block_definitions.h"
#include "chunk.h"
#include "textures.h"
#include "world.h"
#include "abstract/shader.h"
#include "abstract/vertex_buffer.h"
#include "abstract/index_buffer.h"
#include "abstract/vertex_buffer_layout.h"
#include "abstract/vertex_array.h"
#include "abstract/renderer.h"

RepGameState globalGameState;

glm::mat4 model;

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

    globalGameState.block_selection.show = sqrt(                                                                                                                                                     //
                                               ( globalGameState.camera.x - globalGameState.block_selection.destroy_x ) * ( globalGameState.camera.x - globalGameState.block_selection.destroy_x ) + //
                                               ( globalGameState.camera.y - globalGameState.block_selection.destroy_y ) * ( globalGameState.camera.y - globalGameState.block_selection.destroy_y ) + //
                                               ( globalGameState.camera.z - globalGameState.block_selection.destroy_z ) * ( globalGameState.camera.z - globalGameState.block_selection.destroy_z ) ) < REACH_DISTANCE;

    int block_x = globalGameState.block_selection.destroy_x;
    int block_y = globalGameState.block_selection.destroy_y;
    int block_z = globalGameState.block_selection.destroy_z;

    if ( globalGameState.block_selection.show && globalGameState.input.mouse.buttons.middle ) {

        Block *block = world_get_loaded_block( &globalGameState.gameChunks, TRIP_ARGS( globalGameState.block_selection.destroy_ ) );
        if ( block ) {
            BlockDefinition *blockDef = block->blockDef;
            if ( blockDef ) {
                globalGameState.block_selection.blockID = blockDef->id;
            }
        }
    }
    if ( globalGameState.block_selection.show && globalGameState.input.mouse.buttons.left && globalGameState.input.click_delay_left == 0 ) {
        change_block( 0, AIR );
        globalGameState.input.click_delay_left = 8;
    }
    if ( globalGameState.block_selection.show && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        change_block( 1, globalGameState.block_selection.blockID );
        globalGameState.input.click_delay_right = 4;
    }
    if ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x || globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) {
        // pr_debug( "Position Diff:%d %d", input.mouse.currentPosition.x - input.mouse.previousPosition.x, input.mouse.currentPosition.y - input.mouse.previousPosition.y );
    }

    globalGameState.camera.angle_H += ( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x ) * 0.04f;
    globalGameState.camera.angle_V += ( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y ) * 0.04f;

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

    float angle = 0;
    int moved = 0;

    if ( globalGameState.input.arrows.front ) {
        moved = 1;
        if ( globalGameState.input.arrows.left && !globalGameState.input.arrows.right ) {
            angle -= 45;
        }
        if ( globalGameState.input.arrows.right && !globalGameState.input.arrows.left ) {
            angle += 45;
        }
        if ( !globalGameState.input.arrows.right && !globalGameState.input.arrows.left ) {
        }
    }
    if ( globalGameState.input.arrows.back ) {
        moved = 1;
        if ( globalGameState.input.arrows.left && !globalGameState.input.arrows.right ) {
            angle -= 135;
        }
        if ( globalGameState.input.arrows.right && !globalGameState.input.arrows.left ) {
            angle += 135;
        }
        if ( !globalGameState.input.arrows.right && !globalGameState.input.arrows.left ) {
            angle += 180;
        }
    }
    if ( !globalGameState.input.arrows.front && !globalGameState.input.arrows.back ) {
        if ( globalGameState.input.arrows.left && !globalGameState.input.arrows.right ) {
            moved = 1;
            angle -= 90;
        }
        if ( globalGameState.input.arrows.right && !globalGameState.input.arrows.left ) {
            moved = 1;
            angle += 90;
        }
    }

    float lx = sin( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) );
    float ly = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    float lz = -cos( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) );

    globalGameState.camera.mx = sin( ( globalGameState.camera.angle_H + angle ) * ( M_PI / 180 ) );
    globalGameState.camera.my = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    globalGameState.camera.mz = -cos( ( globalGameState.camera.angle_H + angle ) * ( M_PI / 180 ) );

    globalGameState.camera.view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                                                    glm::vec3( lx, ly, lz ),       // Look at look vector
                                                    glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
                                                    );
    globalGameState.camera.view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -globalGameState.camera.x,     //
                                                                                      -globalGameState.camera.y,     //
                                                                                      -globalGameState.camera.z ) ); //

    float movement_vector_x = moved ? globalGameState.camera.mx : 0.0f;
    float movement_vector_y = 0.0f;
    float movement_vector_z = moved ? globalGameState.camera.mz : 0.0f;

    if ( globalGameState.input.arrows.up ) {
        moved = 1;
        movement_vector_y += 1;
    } else if ( !globalGameState.input.arrows.down ) {
        moved = 1;
        movement_vector_y -= GRAVITY_STRENGTH;
    }
    if ( globalGameState.input.arrows.down ) {
        moved = 1;
        movement_vector_y -= 1;
    }

    movement_vector_x *= MOVEMENT_SENSITIVITY;
    movement_vector_y *= MOVEMENT_SENSITIVITY;
    movement_vector_z *= MOVEMENT_SENSITIVITY;

    float character_half_width = PLAYER_WIDTH / 2.0f;
    float character_half_height = PLAYER_HEIGHT / 2.0f;

    float limit_movement_x = 0;
    float limit_movement_y = 0;
    float limit_movement_z = 0;

    // if ( globalGameState.physics.chunk ) {

    //     // The coordinates of the player within the chunk (0 to 7.9999)
    //     int chunk_offset_x = floor( globalGameState.camera.x - globalGameState.physics.chunk->chunk_x * CHUNK_SIZE );
    //     int chunk_offset_y = floor( globalGameState.camera.y - EYE_POSITION_OFFSET - globalGameState.physics.chunk->chunk_y * CHUNK_SIZE );
    //     int chunk_offset_z = floor( globalGameState.camera.z - globalGameState.physics.chunk->chunk_z * CHUNK_SIZE );

    //     // The coordinates of the player within the block (0 to 1)
    //     float block_offset_x = globalGameState.camera.x - floor( globalGameState.camera.x );
    //     float block_offset_y = globalGameState.camera.y - EYE_POSITION_OFFSET - floor( globalGameState.camera.y - EYE_POSITION_OFFSET );
    //     float block_offset_z = globalGameState.camera.z - floor( globalGameState.camera.z );
    //     // pr_debug( "block_offset: %f %f %f", TRIP_ARGS( block_offset_ ) );

    //     for ( int i = -1; i < 2; i++ ) {
    //         for ( int j = -1; j < 2; j++ ) {
    //             for ( int k = -1; k < 2; k++ ) {
    //                 if ( ( ( i != 0 ) + ( j != 0 ) + ( k != 0 ) ) > 1 ) {
    //                     // continue;
    //                 }
    //                 float adjusted_half_width = character_half_width;
    //                 float adjusted_half_height = character_half_height;
    //                 // Using the direction ijk, calculate the players extent in that direction.
    //                 if ( i != 0 && k != 0 ) {
    //                     // We're on a diagional, which means the
    //                     adjusted_half_width = character_half_width / sqrt( 2 );
    //                 } else {
    //                     // We're only in 1 direction, so just use the normal size
    //                 }
    //                 if ( ( i != 0 || k != 0 ) && j == -1 ) {
    //                     adjusted_half_height -= 0.1f;
    //                 }
    //                 // pr_debug( "" );
    //                 // The players extent in that direction
    //                 float diff_x = ( block_offset_x + ( float )i * adjusted_half_width );
    //                 float diff_y = ( block_offset_y + ( float )j * adjusted_half_height );
    //                 float diff_z = ( block_offset_z + ( float )k * adjusted_half_width );
    //                 if ( i == 0 && j == -1 && k == 0 ) {
    //                     // pr_debug( "diff below: %f %f %f", TRIP_ARGS( diff_ ) );
    //                 }
    //                 // If the player extends into this block, we need to check it
    //                 int collide_px = ( diff_x > 1.0f );
    //                 int collide_nx = ( diff_x < 0.0f );
    //                 int collide_py = ( diff_y > 1.0f );
    //                 int collide_ny = ( diff_y < 0.0f );
    //                 int collide_pz = ( diff_z > 1.0f );
    //                 int collide_nz = ( diff_z < 0.0f );

    //                 if ( ( collide_px || collide_nx || i == 0 ) && //
    //                      ( collide_py || collide_ny || j == 0 ) && //
    //                      ( collide_pz || collide_nz || k == 0 ) ) {
    //                     // We need to check this block because it is close enough to us

    //                     int block_position_x = floor( chunk_offset_x + i );
    //                     int block_position_y = floor( chunk_offset_y + j );
    //                     int block_position_z = floor( chunk_offset_z + k );
    //                     Block *block = chunk_get_block(    //
    //                         globalGameState.physics.chunk, //
    //                         block_position_x,              //
    //                         block_position_y,              //
    //                         block_position_z               //
    //                         );
    //                     if ( check_block( block ) ) {
    //                         // pr_debug( "Clip pos %2d %2d %2d : %2d %2d %2d", i, j, k, TRIP_ARGS( block_position_ ) );
    //                         // pr_debug( "Collide p:%d %d %d n:%d %d %d dir:%d %d %d", TRIP_ARGS( collide_p ), //
    //                         //           TRIP_ARGS( collide_n ),                                               //
    //                         //           i, j, k );

    //                         if ( !NO_CLIP ) {

    //                             if ( collide_px && movement_vector_x > 0.0f ) {
    //                                 // movement_vector_x = 0;
    //                                 limit_movement_x = diff_x - 1.0f;
    //                             }
    //                             if ( collide_nx && movement_vector_x < 0.0f ) {
    //                                 // movement_vector_x = 0;
    //                                 limit_movement_x = diff_x;
    //                             }
    //                             if ( collide_py && movement_vector_y > 0.0f ) {
    //                                 // movement_vector_y = 0;
    //                                 limit_movement_y = diff_y - 1.0f;
    //                             }
    //                             if ( collide_ny && movement_vector_y < 0.0f ) {
    //                                 // movement_vector_y = 0;
    //                                 limit_movement_y = diff_y;
    //                             }
    //                             if ( collide_pz && movement_vector_z > 0.0f ) {
    //                                 // movement_vector_z = 0;
    //                                 limit_movement_z = diff_z - 1.0f;
    //                             }
    //                             if ( collide_nz && movement_vector_z < 0.0f ) {
    //                                 // movement_vector_z = 0;
    //                                 limit_movement_z = diff_z;
    //                             }
    //                         }

    //                     } else {
    //                         // pr_debug( "No Block %2d %2d %2d : %2d %2d %2d", i, j, k, TRIP_ARGS( block_position_ ) );
    //                         // pr_debug( "No Block" );
    //                     }
    //                 } else {
    //                     // pr_debug( "Too Far  %2d %2d %2d : %4.2f %4.2f %4.2f", i, j, k, TRIP_ARGS( diff_ ) );
    //                 }
    //             }
    //         }
    //     }

    // float move_mag = sqrt( ( movement_vector_x * movement_vector_x ) + ( movement_vector_y * movement_vector_y ) + ( movement_vector_z * movement_vector_z ) );
    // pr_debug( "                                                    Movement: Mag:%f %f %f %f", move_mag, TRIP_ARGS( movement_vector_ ) );

    // float fraction = 0.020f * MOVEMENT_SENSITIVITY;
    // pr_debug("fraction %f", fraction);
    // movement_vector_x *= fraction;
    // movement_vector_y *= fraction;
    // movement_vector_z *= fraction;

    // pr_debug( "limit:%f %f %f", TRIP_ARGS( limit_movement_ ) );
    // movement_vector_x -= limit_movement_x;
    // movement_vector_y -= limit_movement_y;
    // movement_vector_z -= limit_movement_z;

    globalGameState.camera.x += movement_vector_x;
    globalGameState.camera.y += movement_vector_y;
    globalGameState.camera.z += movement_vector_z;

    // pr_debug( "CameraPos::%f %f %f", globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );

    // }

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
    globalGameState.camera.angle_V = 45.0f;
    globalGameState.camera.x = -1.0f;
    globalGameState.camera.y = PERSON_HEIGHT;
    globalGameState.camera.z = -1.0f;
    globalGameState.block_selection.blockID = TNT;
    world_init( &globalGameState.gameChunks );
    // pr_debug( "RepGame init done" );
}

int check_block( Block *block ) {
    if ( block != NULL ) {
        if ( block->blockDef != NULL ) {
            BlockID blockID = block->blockDef->id;
            if ( !( blockID == AIR || blockID == AIR ) ) {
                return 1;
            }
        } else {
            pr_debug( "No BlockDef!!" );
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
    model = glm::mat4( 1.0f );
    initilizeGameState( );
}

int repgame_shouldExit( ) {
    return globalGameState.input.exitGame;
}

void repgame_changeSize( int w, int h ) {

    pr_debug( "Screen Size Change:%dx%d", w, h );
    globalGameState.screen.width = w;
    globalGameState.screen.height = h;

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;

    glViewport( 0, 0, w, h );
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, 1000.0f );
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

    glm::mat4 mvp = globalGameState.screen.proj * globalGameState.camera.view_look * globalGameState.camera.view_trans * model;

    world_render( &globalGameState.gameChunks, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );
    world_draw( &globalGameState.gameChunks, mvp );
}

void repgame_cleanup( ) {
    world_cleanup( &globalGameState.gameChunks );
    block_definitions_free_definitions( );
    // pr_debug( "RepGame cleanup done" );
}
void repgame_arrowKeyDownInput( int key, int x, int y ) {
    input_arrowKeyDownInput( &globalGameState.input, key, x, y );
}

void repgame_arrowKeyUpInput( int key, int x, int y ) {
    input_arrowKeyUpInput( &globalGameState.input, key, x, y );
}

void repgame_mouseInput( int button, int state, int x, int y ) {
    input_mouseInput( &globalGameState.input, button, state, x, y );
}

void repgame_keysInput( unsigned char key, int x, int y, int pressed ) {
    input_keysInput( &globalGameState.input, key, x, y, pressed );
}

void repgame_mouseMove( int x, int y ) {
    input_mouseMove( &globalGameState.input, x, y );
}