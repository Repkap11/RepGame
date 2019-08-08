#include "common/world.hpp"
#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include <math.h>

MK_SHADER( object_vertex );
MK_SHADER( object_fragment );

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

    shader_init( &world->sky_shader, &object_vertex, &object_fragment );

    sky_box_init( &world->skyBox, &world->vbl_object_vertex, &world->vbl_object_position );
    mobs_init( &world->mobs, &world->vbl_object_vertex, &world->vbl_object_position );
    mouse_selection_init( &world->mouseSelection, &world->vbl_block, &world->vbl_coords );
    lights_init( &world->lights );
    lights_add_light( &world->lights, 5 );
    lights_update_color( &world->lights, 5, 10, 10, 10 );

    lights_add_light( &world->lights, 15 );
    lights_update_color( &world->lights, 15, 100, 100, 100 );
}
void world_render( World *world, TRIP_ARGS( float camera_ ), int limit_render, glm::mat4 &rotation ) {
    chunk_loader_render_chunks( &world->loadedChunks, TRIP_ARGS( camera_ ), limit_render );
    // mobs_update_position( &world->mobs, 10, 10, 10, rotation );
}

void world_set_selected_block( World *world, int selected_x, int selected_y, int selected_z, int shouldDraw ) {
    mouse_selection_set_block( &world->mouseSelection, TRIP_ARGS( selected_ ), shouldDraw );
}

static float light_offset = 0.0f;
static float light_change = 1.0f;
void world_draw( World *world, Texture *blocksTexture, Texture *metallicTexture, glm::mat4 &mvp, glm::mat4 &mvp_sky, int debug, int draw_mouse_selection, //
                 float camera_x, float camera_y, float camera_z ) {

    sky_box_draw( &world->skyBox, &world->renderer, mvp_sky, &world->sky_shader );

    shader_set_uniform1i( &world->sky_shader, "u_Texture", blocksTexture->slot );
    shader_set_uniform_mat4f( &world->sky_shader, "u_MVP", mvp );

    mobs_draw( &world->mobs, &world->renderer, &world->sky_shader );

    shader_set_uniform1i( &world->loadedChunks.shader, "u_Texture", blocksTexture->slot );
    shader_set_uniform1i( &world->loadedChunks.shader, "u_Metallic", metallicTexture->slot );
    shader_set_uniform_mat4f( &world->loadedChunks.shader, "u_MVP", mvp );
    light_offset += light_change;
    if ( light_offset > 2 ) {
        light_change = -0.05;
    }
    if ( light_offset < 0.0 ) {
        light_change = 0.05;
    }
    // pr_debug( "Light offset:%f", light_offset );
    glm::mat4 rotation = glm::mat4( 1 );
    float x = 50;
    float y = 10 + light_offset;
    float z = 0;
    mobs_update_position( &world->mobs, 4, x, y, z, rotation );
    lights_update_position( &world->lights, 15, x, y, z );

    lights_set_uniforms( &world->lights, &world->loadedChunks.shader );
    shader_set_uniform3f( &world->loadedChunks.shader, "u_CameraPos", camera_x, camera_y, camera_z );

    float debug_block_scale;
    if ( debug ) {
        debug_block_scale = BLOCK_SCALE_OFFSET;
    } else {
        debug_block_scale = 0.0f;
    }
    shader_set_uniform3f( &world->loadedChunks.shader, "u_DebugScaleOffset", debug_block_scale, debug_block_scale, debug_block_scale );
    chunk_loader_calculate_cull( &world->loadedChunks, mvp );
    if ( draw_mouse_selection ) {
        mouse_selection_draw( &world->mouseSelection, &world->renderer, &world->loadedChunks.shader );
    }
    chunk_loader_draw_chunks( &world->loadedChunks, &world->renderer, mvp );
}
void world_cleanup( World *world ) {

    chunk_loader_cleanup( &world->loadedChunks );
    sky_box_destroy( &world->skyBox );
    mobs_cleanup( &world->mobs );
    vertex_buffer_layout_destroy( &world->vbl_block );
    vertex_buffer_layout_destroy( &world->vbl_block );
    vertex_buffer_layout_destroy( &world->vbl_coords );
    lights_cleanup( &world->lights );
}

void fixup_chunk( World *world, Chunk *chunk, TRIP_ARGS( int offset_ ), TRIP_ARGS( int pos_ ), BlockState blockState ) {
    // pr_debug( "Fixup Offset: %d %d %d", x, y, z );
    Chunk *fixupChunk = chunk_loader_get_chunk( &world->loadedChunks, chunk->chunk_x + offset_x, chunk->chunk_y + offset_y, chunk->chunk_z + offset_z );
    if ( fixupChunk ) {
        chunk_unprogram_terrain( chunk );
        chunk_set_block( fixupChunk, TRIP_ARGS( pos_ ), blockState );
        fixupChunk->ditry = 1;
        chunk_calculate_popupated_blocks( fixupChunk );
        chunk_program_terrain( fixupChunk );
    }
}

Chunk *world_get_loaded_chunk( World *world, TRIP_ARGS( int block_ ) ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );
    return chunk_loader_get_chunk( &world->loadedChunks, TRIP_ARGS( chunk_ ) );
}

BlockState world_get_block_from_chunk( Chunk *chunk, TRIP_ARGS( int block_ ) ) {
    int diff_x = block_x - chunk->chunk_x * CHUNK_SIZE;
    int diff_y = block_y - chunk->chunk_y * CHUNK_SIZE;
    int diff_z = block_z - chunk->chunk_z * CHUNK_SIZE;
    return chunk_get_block( chunk, TRIP_ARGS( diff_ ) );
}

BlockState world_get_loaded_block( World *world, TRIP_ARGS( int block_ ) ) {
    Chunk *chunk = world_get_loaded_chunk( world, TRIP_ARGS( block_ ) );
    if ( chunk ) {
        if ( chunk->is_loading ) {
            return {LAST_BLOCK_ID, BLOCK_ROTATE_0};
        }
        BlockState blockState = world_get_block_from_chunk( chunk, TRIP_ARGS( block_ ) );
        return blockState;
    }
    return {LAST_BLOCK_ID, BLOCK_ROTATE_0};
}

void world_set_loaded_block( World *world, TRIP_ARGS( int block_ ), BlockState blockState ) {
    int chunk_x = floor( block_x / ( float )CHUNK_SIZE );
    int chunk_y = floor( block_y / ( float )CHUNK_SIZE );
    int chunk_z = floor( block_z / ( float )CHUNK_SIZE );

    Chunk *chunk = chunk_loader_get_chunk( &world->loadedChunks, TRIP_ARGS( chunk_ ) );
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
        chunk_unprogram_terrain( chunk );
        chunk_set_block( chunk, TRIP_ARGS( diff_ ), blockState );
        chunk->ditry = 1;
        chunk_calculate_popupated_blocks( chunk );
        chunk_program_terrain( chunk );
    } else {
        // This just means mouse is not pointing at a block
        // pr_debug( "Could not find the pointed to chunk" );
    }
}