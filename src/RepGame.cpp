
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

    float lx = sin( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) );
    float ly = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    float lz = -cos( ( globalGameState.camera.angle_H ) * ( M_PI / 180 ) );

    globalGameState.camera.mx = sin( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );
    globalGameState.camera.my = -tan( globalGameState.camera.angle_V * ( M_PI / 180 ) );
    globalGameState.camera.mz = -cos( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * ( M_PI / 180 ) );

    globalGameState.camera.view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                                                    glm::vec3( lx, ly, lz ),       // Look at look vector
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

    float character_half_width = PLAYER_WIDTH / 2.0f;
    float character_half_height = PLAYER_HEIGHT / 2.0f;

    float limit_movement_x = 0;
    float limit_movement_y = 0;
    float limit_movement_z = 0;

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

GLuint pbo[ 2 ];
int pbo_size = 4 * sizeof( int );

void repgame_init( ) {
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBlendEquation( GL_FUNC_ADD );
    model = glm::mat4( 1.0f );
    initilizeGameState( );
    showErrors( );

    {
        // init the PBOs
        glGenBuffers( 2, pbo );
        glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ 0 ] );
        glBufferData( GL_PIXEL_PACK_BUFFER, pbo_size, NULL, GL_STREAM_READ );

        glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ 1 ] );
        glBufferData( GL_PIXEL_PACK_BUFFER, pbo_size, NULL, GL_STREAM_READ );

        // bind it to nothing so other stuff doesn't
        // think it should use the PBOs
        glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
    }
}

void repgame_set_textures( unsigned char *textures, int textures_len ) {
    textures_set_texture_data( textures, textures_len );
}

int repgame_shouldExit( ) {
    return globalGameState.input.exitGame;
}

GLuint fbo;

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
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, 1000.0f );

    if ( fbo != 0 ) {
        glDeleteFramebuffers( 1, &fbo );
    }
    // generate a framebuffer
    glGenFramebuffers( 1, &fbo );
    // bind it as the target for rendering commands
    glBindFramebuffer( GL_FRAMEBUFFER, fbo );

    GLuint render_buffer_color;
    showErrors( );
    glGenRenderbuffers( 1, &render_buffer_color );
    showErrors( );
    glBindRenderbuffer( GL_RENDERBUFFER, render_buffer_color );
    showErrors( );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA8, globalGameState.screen.width, globalGameState.screen.height );
    showErrors( );
    glFramebufferRenderbuffer( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_buffer_color );
    showErrors( );

    GLuint render_buffer_depth;
    glGenRenderbuffers( 1, &render_buffer_depth );
    showErrors( );
    glBindRenderbuffer( GL_RENDERBUFFER, render_buffer_depth );
    showErrors( );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, globalGameState.screen.width, globalGameState.screen.height );
    showErrors( );
    glFramebufferRenderbuffer( GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_buffer_depth );
    showErrors( );

    GLuint render_buffer_color_metadata;
    showErrors( );
    glGenRenderbuffers( 1, &render_buffer_color_metadata );
    showErrors( );
    glBindRenderbuffer( GL_RENDERBUFFER, render_buffer_color_metadata );
    showErrors( );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA32I, globalGameState.screen.width, globalGameState.screen.height );
    showErrors( );
    glFramebufferRenderbuffer( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, render_buffer_color_metadata );
    showErrors( );

    GLuint fbdraw;
    const GLenum bufs[ 2 ] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    // glGenDrawbuffers( 1, &fbdraw );
    glDrawBuffers( 2, bufs );
    showErrors( );

    GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
    if ( status != GL_FRAMEBUFFER_COMPLETE ) {
        pr_debug( "The frame buffer status is not complete 0x%x", status );
        exit( 1 );
    } else {
        pr_debug( "Frame buffer OK" );
    }
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

int index = 0;
int nextIndex = 0;
void repgame_draw( ) {

    glm::mat4 mvp = globalGameState.screen.proj * globalGameState.camera.view_look * globalGameState.camera.view_trans * model;

    glBindFramebuffer( GL_FRAMEBUFFER, fbo );
    showErrors( );

    world_render( &globalGameState.gameChunks, globalGameState.camera.x, globalGameState.camera.y, globalGameState.camera.z );
    showErrors( );

    world_draw( &globalGameState.gameChunks, mvp );
    showErrors( );

    struct timespec t_start = {0, 0}, t_end = {0, 0};
    {
        index = ( index + 1 ) % 2;
        nextIndex = ( index + 1 ) % 2;

        glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ nextIndex ] );
        glReadBuffer( GL_COLOR_ATTACHMENT1 );
        glBufferData( GL_PIXEL_PACK_BUFFER, pbo_size, NULL, GL_STREAM_READ );

        clock_gettime( CLOCK_MONOTONIC, &t_start );
        // int outDataRead[] = {42, 43, 44, 45};
        glReadPixels( globalGameState.screen.width / 2, globalGameState.screen.height / 2, 1, 1, GL_RGBA_INTEGER, GL_INT, 0 );
        clock_gettime( CLOCK_MONOTONIC, &t_end );
        double diff_read = ( ( ( double )t_end.tv_sec + 1.0e-9 * t_end.tv_nsec ) - ( ( double )t_start.tv_sec + 1.0e-9 * t_start.tv_nsec ) ) * 1000.0;
        // pr_debug( "Depth at center r:%d g:%d b:%d a:%d", outDataRead[ 0 ], outDataRead[ 1 ], outDataRead[ 2 ], outDataRead[ 3 ] );
        showErrors( );

        glBindBuffer( GL_PIXEL_PACK_BUFFER, pbo[ index ] );

        int *outData = 0;
        clock_gettime( CLOCK_MONOTONIC, &t_start );

        outData = ( int * )glMapBufferRange( GL_PIXEL_PACK_BUFFER, 0, 4 * sizeof( int ), GL_MAP_READ_BIT );
        clock_gettime( CLOCK_MONOTONIC, &t_end );
        double diff_map = ( ( ( double )t_end.tv_sec + 1.0e-9 * t_end.tv_nsec ) - ( ( double )t_start.tv_sec + 1.0e-9 * t_start.tv_nsec ) ) * 1000.0;
        pr_debug( "Time read:%fms map:%fms", diff_read, diff_map );

        if ( outData ) {
            // pr_debug( "Depth at center r:%d g:%d b:%d a:%d", outData[ 0 ], outData[ 1 ], outData[ 2 ], outData[ 3 ] );
            glUnmapBuffer( GL_PIXEL_PACK_BUFFER );
        } else {
            pr_debug( "No Data" );
        }
        showErrors( );

        glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
        showErrors( );

        // pr_debug( "Got 1" );
    }

    /* We are going to blit into the window (default framebuffer)                     */
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
    showErrors( );
    // pr_debug( "Got 2" );

    GLenum which_buf = GL_BACK;
    glDrawBuffers( 1, &which_buf );
    showErrors( );

    /* Read from your FBO */
    glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
    showErrors( );

    glReadBuffer( GL_COLOR_ATTACHMENT0 ); /* Use Color Attachment 0 as color src. */
    showErrors( );
    // pr_debug( "Got 3 %p", glBlitFramebuffer );
    int result;
    glGetIntegerv( GL_MAX_DRAW_BUFFERS, &result );
    // pr_debug( "Got 4 num_bufs:%d", result );
    showErrors( );

    /* Copy the color and depth buffer from your FBO to the default framebuffer       */
    glBlitFramebuffer( 0, 0, 2160, 1080, //
                       0, 0, 2160, 1080, //
                       GL_COLOR_BUFFER_BIT, GL_NEAREST );
    // pr_debug( "Got 5" );

    showErrors( );
    glBindFramebuffer( GL_FRAMEBUFFER, fbo );
    showErrors( );
}

void repgame_cleanup( ) {
    world_cleanup( &globalGameState.gameChunks );
    block_definitions_free_definitions( );
    // pr_debug( "RepGame cleanup done" );
}

InputState *repgame_getInputState( ) {
    return &globalGameState.input;
}