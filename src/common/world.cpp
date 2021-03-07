#include <math.h>

#include "common/world.hpp"
#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"

#define TINT_UNDER_WATER_OBJECT_NEVER 0
#define TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL 1
#define TINT_UNDER_WATER_OBJECT_ALWAYS 2

MK_SHADER( object_vertex );
MK_SHADER( object_fragment );

#if ( USES_FRAME_BUFFER )
bool USES_FSQ_AND_FB = true;
#else
bool USES_FSQ_AND_FB = false;
#endif

void world_init( World *world, TRIP_ARGS( float camera_ ) ) {
    // These are from CubeFace
    vertex_buffer_layout_init( &world->vbl_block );
    vertex_buffer_layout_push_float( &world->vbl_block, 3 );        // Coords
    vertex_buffer_layout_push_float( &world->vbl_block, 2 );        // Texture coords
    vertex_buffer_layout_push_unsigned_int( &world->vbl_block, 1 ); // Face type (top, sides, bottom)
    vertex_buffer_layout_push_unsigned_int( &world->vbl_block, 1 ); // Corner_shift

    // These are from BlockCoords
    vertex_buffer_layout_init( &world->vbl_coords );
    vertex_buffer_layout_push_float( &world->vbl_coords, 3 );        // block 3d world coords
    vertex_buffer_layout_push_unsigned_int( &world->vbl_coords, 1 ); // Multiples (mesh)
    vertex_buffer_layout_push_unsigned_int( &world->vbl_coords, 3 ); // which texture
    vertex_buffer_layout_push_unsigned_int( &world->vbl_coords, 3 ); // packed lighting
    vertex_buffer_layout_push_unsigned_int( &world->vbl_coords, 3 ); // packed lightingMOB_ROTATION
    vertex_buffer_layout_push_unsigned_int( &world->vbl_coords, 1 ); // face_shift for face rotation
    vertex_buffer_layout_push_float( &world->vbl_coords, 3 );        // block shape scale
    vertex_buffer_layout_push_float( &world->vbl_coords, 3 );        // block shape offset
    vertex_buffer_layout_push_float( &world->vbl_coords, 3 );        // texture shape offset

    // These are from ObjectVertex
    vertex_buffer_layout_init( &world->vbl_object_vertex );
    vertex_buffer_layout_push_float( &world->vbl_object_vertex, 3 );        // Coords
    vertex_buffer_layout_push_float( &world->vbl_object_vertex, 2 );        // TxCoords
    vertex_buffer_layout_push_unsigned_int( &world->vbl_object_vertex, 1 ); // faceType

    // These are from ObjectPosition
    vertex_buffer_layout_init( &world->vbl_object_position );
    vertex_buffer_layout_push_unsigned_int( &world->vbl_object_position, 1 ); // id
    vertex_buffer_layout_push_unsigned_int( &world->vbl_object_position, 3 ); // which texture
    vertex_buffer_layout_push_float( &world->vbl_object_position, 4 );        // transform
    vertex_buffer_layout_push_float( &world->vbl_object_position, 4 );        // transform
    vertex_buffer_layout_push_float( &world->vbl_object_position, 4 );        // transform
    vertex_buffer_layout_push_float( &world->vbl_object_position, 4 );        // transform

    chunk_loader_init( &world->loadedChunks, TRIP_ARGS( camera_ ), &world->vbl_block, &world->vbl_coords );

    float *random_rotation_blocks;
    block_definitions_get_random_rotations( &random_rotation_blocks );

    shader_set_uniform1fv( &world->loadedChunks.shader, "u_RandomRotationBlocks", random_rotation_blocks, MAX_ROTATABLE_BLOCK );

    shader_init( &world->object_shader, &object_vertex, &object_fragment );

    sky_box_init( &world->skyBox, &world->vbl_object_vertex, &world->vbl_object_position );
    world->mobs = Mobs( &world->vbl_object_vertex, &world->vbl_object_position );
    mouse_selection_init( &world->mouseSelection, &world->vbl_block, &world->vbl_coords );

    if ( USES_FSQ_AND_FB ) {
        full_screen_quad_init( &world->fullScreenQuad );
        texture_init_empty_color( &world->reflectionTexture, 0 );
        texture_init_empty_color( &world->blockTexture, 0 );
        texture_init_empty_depth_stencil( &world->depthStencilTexture, 0 );

        frame_buffer_init( &world->frameBuffer );
        frame_buffer_attach_texture( &world->frameBuffer, &world->blockTexture, 0 );
        frame_buffer_attach_texture( &world->frameBuffer, &world->reflectionTexture, 1 );
        frame_buffer_attach_texture( &world->frameBuffer, &world->depthStencilTexture, 0 ); // 0 is fake

        showErrors( );
        frame_buffer_bind_display( );
    }
    showErrors( );
}

void world_change_size( World *world, int width, int height ) {
    world->screenWidth = width;
    world->screenHeight = height;
    texture_change_size( &world->blockTexture, width, height );
    texture_change_size( &world->reflectionTexture, width, height );
    texture_change_size( &world->depthStencilTexture, width, height );

    if ( !frame_buffer_ok( &world->frameBuffer ) ) {
        pr_debug( "Frame buffer not ok" );
        showErrors( );
        exit( 1 );
    }
}

void world_render( World *world, TRIP_ARGS( float camera_ ), int limit_render, const glm::mat4 &rotation ) {
    chunk_loader_render_chunks( &world->loadedChunks, TRIP_ARGS( camera_ ), limit_render );
    // world->mobs.update_position( 10, 10, 10, rotation );
}

void world_set_selected_block( World *world, int selected_x, int selected_y, int selected_z, int shouldDraw ) {
    BlockState blockState = world_get_loaded_block( world, TRIP_ARGS( selected_ ) );
    if ( blockState.id == LAST_BLOCK_ID ) {
        return;
    }
    mouse_selection_set_block( &world->mouseSelection, TRIP_ARGS( selected_ ), shouldDraw, blockState );
}

#define WATER_THRESHOLD_P 0.02
#define WATER_THRESHOLD_N -0.01
void world_draw( World *world, Texture *blocksTexture, const glm::mat4 &mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater ) {
    int object_water_tint_type;
    int block_water_tint_type = headInWater ? TINT_UNDER_WATER_OBJECT_ALWAYS : TINT_UNDER_WATER_OBJECT_NEVER;
    double out_int_part;
    float water_diff = modf( y_height, &out_int_part ) - ( WATER_HEIGHT );
    if ( headInWater ) {
        if ( water_diff > WATER_THRESHOLD_N && water_diff < WATER_THRESHOLD_P ) {
            object_water_tint_type = TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL;
            block_water_tint_type = TINT_UNDER_WATER_OBJECT_UNDER_Y_LEVEL;
        } else if ( water_diff < WATER_THRESHOLD_N ) {
            object_water_tint_type = TINT_UNDER_WATER_OBJECT_ALWAYS;
            block_water_tint_type = TINT_UNDER_WATER_OBJECT_ALWAYS;
        } else {
            object_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
            block_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
        }
    } else {
        object_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
        block_water_tint_type = TINT_UNDER_WATER_OBJECT_NEVER;
    }
    float debug_block_scale;
    if ( debug ) {
        debug_block_scale = BLOCK_SCALE_OFFSET;
    } else {
        debug_block_scale = 0.0f;
    }

    if ( USES_FSQ_AND_FB ) {
        frame_buffer_bind( &world->frameBuffer );
        glViewport( 0, 0, world->screenWidth, world->screenHeight );

        // glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
        // glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
        // glClearDepth
        showErrors( );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
        GLenum bufs[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        // glDrawBuffers( 1, bufs );
        glDrawBuffers( 2, bufs );
        showErrors( );
    }

    shader_set_uniform1i( &world->object_shader, "u_DrawToReflection", 0 );
    shader_set_uniform1i( &world->object_shader, "u_Texture", blocksTexture->slot );
    shader_set_uniform1f( &world->object_shader, "u_ReflectionHeight", 0 );
    shader_set_uniform1i( &world->object_shader, "u_TintUnderWater", object_water_tint_type );
    shader_set_uniform1f( &world->object_shader, "u_ExtraAlpha", 1.0f );
    world->mobs.draw( mvp, &world->renderer, &world->object_shader );                 // Mobs
    sky_box_draw( &world->skyBox, &world->renderer, mvp_sky, &world->object_shader ); // Sky
    glEnable( GL_DEPTH_TEST );

    chunk_loader_calculate_cull( &world->loadedChunks, mvp, false );
    shader_set_uniform1f( &world->loadedChunks.shader, "u_ExtraAlpha", 1.0f );
    shader_set_uniform1i( &world->loadedChunks.shader, "u_DrawToReflection", 0 );
    shader_set_uniform1f( &world->loadedChunks.shader, "u_ReflectionHeight", 0 );
    shader_set_uniform1i( &world->loadedChunks.shader, "u_Texture", blocksTexture->slot );
    shader_set_uniform3f( &world->loadedChunks.shader, "u_DebugScaleOffset", debug_block_scale, debug_block_scale, debug_block_scale );
    shader_set_uniform1ui( &world->loadedChunks.shader, "u_ScaleTextureBlock", 1 );
    shader_set_uniform1i( &world->loadedChunks.shader, "u_TintUnderWater", block_water_tint_type );
    shader_set_uniform1f( &world->loadedChunks.shader, "u_ReflectionDotSign", y_height < 0 ? -1.0f : 1.0f );
    // shader_set_uniform1f( &world->loadedChunks.shader, "u_ReflectionDotSign", 1.0f );
    chunk_loader_draw_chunks( &world->loadedChunks, mvp, &world->renderer, false, false ); // Blocks

    shader_set_uniform1i( &world->loadedChunks.shader, "u_TintUnderWater", 0 );
    if ( draw_mouse_selection ) {
        mouse_selection_draw( &world->mouseSelection, &world->renderer, &world->loadedChunks.shader );
    }

    glEnable( GL_STENCIL_TEST );
    glStencilFunc( GL_ALWAYS, 1, 0xff );
    glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );

    chunk_loader_draw_chunks( &world->loadedChunks, mvp, &world->renderer, true, false ); // Stencil water
    glStencilFunc( GL_EQUAL, 1, 0xff );
    glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

    if ( USES_FSQ_AND_FB ) {

        glCullFace( GL_FRONT );
        glClear( GL_DEPTH_BUFFER_BIT );
        float offset = 1.0 - WATER_HEIGHT;
        shader_set_uniform1i( &world->loadedChunks.shader, "u_DrawToReflection", USES_FSQ_AND_FB );
        shader_set_uniform1f( &world->loadedChunks.shader, "u_ExtraAlpha", USES_FSQ_AND_FB ? 1.0f : 0.5f ); // this make reflections not solid...
        shader_set_uniform1f( &world->loadedChunks.shader, "u_ReflectionHeight", offset );
        shader_set_uniform1i( &world->loadedChunks.shader, "u_TintUnderWater", block_water_tint_type );
        chunk_loader_calculate_cull( &world->loadedChunks, mvp_reflect, true );
        chunk_loader_draw_chunks( &world->loadedChunks, mvp_reflect, &world->renderer, false, true ); // Reflected blocks

        shader_set_uniform1i( &world->object_shader, "u_Texture", blocksTexture->slot );
        shader_set_uniform1f( &world->object_shader, "u_ReflectionHeight", offset );
        shader_set_uniform1f( &world->object_shader, "u_ExtraAlpha", USES_FSQ_AND_FB ? 1.0f : 0.5f );
        shader_set_uniform1i( &world->object_shader, "u_DrawToReflection", USES_FSQ_AND_FB );
        world->mobs.draw( mvp_reflect, &world->renderer, &world->object_shader );                 // Reflected mobs
        sky_box_draw( &world->skyBox, &world->renderer, mvp_sky_reflect, &world->object_shader ); // Reflected sky

        shader_set_uniform1f( &world->loadedChunks.shader, "u_ReflectionHeight", 0 );
        shader_set_uniform1f( &world->object_shader, "u_ReflectionHeight", 0 );

        glCullFace( GL_BACK );
    }
    glDisable( GL_STENCIL_TEST );

    if ( USES_FSQ_AND_FB ) {
        frame_buffer_bind_display( );
        glViewport( 0, 0, world->screenWidth, world->screenHeight );
        showErrors( );
        glDisable( GL_DEPTH_TEST );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

        full_screen_quad_draw_texture( &world->fullScreenQuad, &world->renderer, &world->blockTexture, 1.0, false, world->screenWidth, world->screenHeight );
        full_screen_quad_draw_texture( &world->fullScreenQuad, &world->renderer, &world->reflectionTexture, y_height < 0 ? 0.1 : 0.2, true, world->screenWidth, world->screenHeight );
        // full_screen_quad_draw_texture( &world->fullScreenQuad, &world->renderer, &world->frameBuffer.depthStencilTexture );
        glEnable( GL_DEPTH_TEST );
    }
    // chunk_loader_draw_chunks( &world->loadedChunks, mvp, &world->renderer, true, false ); // Water
    glDisable( GL_STENCIL_TEST );
}
void world_cleanup( World *world ) {

    chunk_loader_cleanup( &world->loadedChunks );
    sky_box_destroy( &world->skyBox );
    world->mobs.cleanup( );
    vertex_buffer_layout_destroy( &world->vbl_block );
    vertex_buffer_layout_destroy( &world->vbl_block );
    vertex_buffer_layout_destroy( &world->vbl_coords );
    frame_buffer_destroy( &world->frameBuffer );
    if ( USES_FSQ_AND_FB ) {
        texture_destroy( &world->blockTexture );
        texture_destroy( &world->reflectionTexture );
        texture_destroy( &world->depthStencilTexture );
        full_screen_quad_destroy( &world->fullScreenQuad );
    }
}

int can_fixup_chunk( World *world, Chunk *chunk, TRIP_ARGS( int offset_ ) ) {
    Chunk *fixupChunk = chunk_loader_get_chunk( &world->loadedChunks, chunk->chunk_x + offset_x, chunk->chunk_y + offset_y, chunk->chunk_z + offset_z );
    if ( fixupChunk ) {
        if ( !fixupChunk->is_loading ) {
            return 1;
        }
    }
    return 0;
}

void fixup_chunk( World *world, Chunk *chunk, TRIP_ARGS( int offset_ ), TRIP_ARGS( int pos_ ), BlockState blockState ) {
    // pr_debug( "Fixup Offset: %d %d %d", x, y, z );
    Chunk *fixupChunk = chunk_loader_get_chunk( &world->loadedChunks, chunk->chunk_x + offset_x, chunk->chunk_y + offset_y, chunk->chunk_z + offset_z );
    if ( fixupChunk ) {
        if ( fixupChunk->is_loading ) {
            // This shouldn't happen because can_fixup_chunk should be called first.
            pr_debug( "Ekk, still loading. You'll probably get a lighting bug." );
            return;
        }
        chunk_set_block( fixupChunk, TRIP_ARGS( pos_ ), blockState );
        fixupChunk->dirty = 1;
        fixupChunk->needs_repopulation = 1;
    }
}

Chunk *world_get_loaded_chunk( World *world, TRIP_ARGS( int block_ ) ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );
    return chunk_loader_get_chunk( &world->loadedChunks, chunk_x, chunk_y, chunk_z );
}

BlockState world_get_block_from_chunk( Chunk *chunk, TRIP_ARGS( int block_ ) ) {
    int diff_x = block_x - chunk->chunk_x * CHUNK_SIZE;
    int diff_y = block_y - chunk->chunk_y * CHUNK_SIZE;
    int diff_z = block_z - chunk->chunk_z * CHUNK_SIZE;
    return chunk_get_block( chunk, diff_x, diff_y, diff_z );
}

BlockState world_get_loaded_block( World *world, TRIP_ARGS( int block_ ) ) {
    Chunk *chunk = world_get_loaded_chunk( world, TRIP_ARGS( block_ ) );
    if ( chunk ) {
        if ( chunk->is_loading ) {
            return BLOCK_STATE_LAST_BLOCK_ID;
        }
        BlockState blockState = world_get_block_from_chunk( chunk, TRIP_ARGS( block_ ) );
        return blockState;
    }
    return BLOCK_STATE_LAST_BLOCK_ID;
}

void world_set_loaded_block( World *world, TRIP_ARGS( int block_ ), BlockState blockState ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );

    Chunk *chunk = chunk_loader_get_chunk( &world->loadedChunks, chunk_x, chunk_y, chunk_z );
    if ( chunk ) {
        int diff_x = block_x - chunk_x * CHUNK_SIZE;
        int diff_y = block_y - chunk_y * CHUNK_SIZE;
        int diff_z = block_z - chunk_z * CHUNK_SIZE;
        // pr_debug( "Orig Offset: %d %d %d", diff_x, diff_y, diff_z );

        for ( int i = -1; i < 2; i++ ) {
            for ( int j = -1; j < 2; j++ ) {
                for ( int k = -1; k < 2; k++ ) {
                    int needs_update_x = ( ( i != 1 && diff_x == 0 ) || ( i != -1 && diff_x == ( CHUNK_SIZE - 1 ) ) ) || i == 0; //
                    int needs_update_y = ( ( j != 1 && diff_y == 0 ) || ( j != -1 && diff_y == ( CHUNK_SIZE - 1 ) ) ) || j == 0; //
                    int needs_update_z = ( ( k != 1 && diff_z == 0 ) || ( k != -1 && diff_z == ( CHUNK_SIZE - 1 ) ) ) || k == 0;
                    int needs_update = TRIP_AND( needs_update_ ) && !( i == 0 && j == 0 && k == 0 );
                    if ( needs_update ) {
                        if ( !can_fixup_chunk( world, chunk, i, j, k ) ) {
                            pr_debug( "Ekk, can't fixup block, so not placing" );
                            return;
                        }
                    }
                }
            }
        }
        for ( int i = -1; i < 2; i++ ) {
            for ( int j = -1; j < 2; j++ ) {
                for ( int k = -1; k < 2; k++ ) {

                    int needs_update_x = ( ( i != 1 && diff_x == 0 ) || ( i != -1 && diff_x == ( CHUNK_SIZE - 1 ) ) ) || i == 0; //
                    int needs_update_y = ( ( j != 1 && diff_y == 0 ) || ( j != -1 && diff_y == ( CHUNK_SIZE - 1 ) ) ) || j == 0; //
                    int needs_update_z = ( ( k != 1 && diff_z == 0 ) || ( k != -1 && diff_z == ( CHUNK_SIZE - 1 ) ) ) || k == 0;

                    int new_i = i * needs_update_x;
                    int new_j = j * needs_update_y;
                    int new_k = k * needs_update_z;

                    int needs_update = TRIP_AND( needs_update_ ) && !( i == 0 && j == 0 && k == 0 );
                    // pr_debug( "Chunk Dir: %d %d %d:%d", i, j, k, needs_update );
                    // pr_debug( "                    Needs Updates: %d %d %d:%d", needs_update_x, needs_update_y, needs_update_z, needs_update );
                    // pr_debug( "                                New Offset: %d %d %d:%d", new_i, new_j, new_k, needs_update );

                    if ( needs_update ) {
                        fixup_chunk( world, chunk, i, j, k, diff_x - CHUNK_SIZE * new_i, diff_y - CHUNK_SIZE * new_j, diff_z - CHUNK_SIZE * new_k, blockState );
                    }
                }
            }
        }
        chunk_set_block( chunk, TRIP_ARGS( diff_ ), blockState );
        chunk->dirty = 1;
        chunk->needs_repopulation = 1;

    } else {
        // This just means mouse is not pointing at a block
        // pr_debug( "Could not find the pointed to chunk" );
    }
}
