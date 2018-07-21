#include "RepGame.h"
#include "chunk.h"
#include "map_gen.h"
#include "map_storage.h"

unsigned int ib_data_solid[] = {
    2,  1,  0, // Front
    0,  3,  2, //

    14, 13, 9,  // Right
    9,  10, 14, //

    7,  4,  5, // Back
    5,  6,  7, //

    11, 8,  12, // Left
    12, 15, 11, //

    22, 18, 19, // Top
    19, 23, 22, //

    17, 21, 20, // Bottom
    20, 16, 17, //
};
unsigned int ib_data_solid_size = 6 * 3 * 2;
unsigned int ib_data_solid_coord_size = 3;

unsigned int ib_data_water[] = {
    2, 0, 1, // Top from the top
    1, 3, 2, //

    2, 3, 1, // Top, from the bottom
    1, 0, 2, //
};
unsigned int ib_data_water_size = 2 * 3 * 2;

int chunk_get_index_from_coords( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL + ( x + 1 ) * CHUNK_SIZE_INTERNAL + ( z + 1 );
}

int chunk_get_coords_from_index( int index, int *out_x, int *out_y, int *out_z ) {
    int y = ( index / ( CHUNK_SIZE_INTERNAL * CHUNK_SIZE_INTERNAL ) ) - 1;
    int x = ( ( index / CHUNK_SIZE_INTERNAL ) % CHUNK_SIZE_INTERNAL ) - 1;
    int z = ( index % ( CHUNK_SIZE_INTERNAL ) ) - 1;
    // return 1;
    int result = x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE && y < CHUNK_SIZE && z < CHUNK_SIZE;
    *out_x = x;
    *out_y = y;
    *out_z = z;
    return result;
}

void chunk_init( Chunk *chunk, VertexBuffer *vb_block_solid, VertexBuffer *vb_block_water, VertexBufferLayout *vbl_block ) {
    chunk->blocks = ( Block * )calloc( CHUNK_BLOCK_SIZE, sizeof( Block ) );
    chunk->solid.populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    chunk->water.populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    {
        vertex_buffer_layout_init( &chunk->vbl_coords );
        vertex_buffer_layout_push_float( &chunk->vbl_coords, 3 );        // block 3d world coords
        vertex_buffer_layout_push_unsigned_int( &chunk->vbl_coords, 3 ); // which texture (block type)
    }

    {
        index_buffer_init( &chunk->solid.ib, ib_data_solid, ib_data_solid_size );
        vertex_buffer_init( &chunk->solid.vb_coords );
        vertex_array_init( &chunk->solid.va );
        vertex_array_add_buffer( &chunk->solid.va, vb_block_solid, vbl_block, 0 );
        vertex_array_add_buffer( &chunk->solid.va, &chunk->solid.vb_coords, &chunk->vbl_coords, 1 );
    }
    {
        index_buffer_init( &chunk->water.ib, ib_data_water, ib_data_water_size );
        vertex_buffer_init( &chunk->water.vb_coords );
        vertex_array_init( &chunk->water.va );
        vertex_array_add_buffer( &chunk->water.va, vb_block_water, vbl_block, 0 );
        vertex_array_add_buffer( &chunk->water.va, &chunk->water.vb_coords, &chunk->vbl_coords, 1 );
    }
}

void chunk_destroy( Chunk *chunk ) {
    free( chunk->blocks );
    free( chunk->solid.populated_blocks );
    free( chunk->water.populated_blocks );
}

void chunk_render_solid( const Chunk *chunk, const Renderer *renderer, const Shader *shader ) {
    if ( chunk->should_render && chunk->solid.num_instances != 0 ) {
        if ( chunk->is_loading ) {
            pr_debug( "Error, attempting to render loading chunk" );
        }
        renderer_draw( renderer, &chunk->solid.va, &chunk->solid.ib, shader, chunk->solid.num_instances );
    }
}

void chunk_render_water( const Chunk *chunk, const Renderer *renderer, const Shader *shader ) {
    if ( chunk->should_render && chunk->water.num_instances != 0 ) {
        if ( chunk->is_loading ) {
            pr_debug( "Error, attempting to render loading chunk" );
        }
        renderer_draw( renderer, &chunk->water.va, &chunk->water.ib, shader, chunk->water.num_instances );
    }
}

Block *chunk_get_block( Chunk *chunk, int x, int y, int z ) {
    if ( chunk->blocks == NULL ) {
        // pr_debug("Chunk has no blocks");
        return NULL;
    }
    if ( x > CHUNK_SIZE + 1 || //
         y > CHUNK_SIZE + 1 || //
         z > CHUNK_SIZE + 1 ) {
        return NULL;
    }
    if ( x < -1 || //
         y < -1 || //
         z < -1 ) {
        return NULL;
    }
    return &chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockID blockID ) {
    Block *block = chunk_get_block( chunk, x, y, z );
    block->blockDef = block_definition_get_definition( blockID );
}

void chunk_persist( Chunk *chunk ) {
    map_storage_persist( chunk );
}

void chunk_load_terrain( Chunk *chunk ) {
    // pr_debug( "Loading chunk terrain x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    int loaded = map_storage_load( chunk );
    if ( !loaded ) {
        // We havn't loaded this chunk before, map gen it.
        map_gen_load_block( chunk );
        chunk->ditry = PERSIST_ALL_CHUNKS;
    }
    int num_water_instances = 0;
    int num_solid_instances = 0;
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( drawn_block ) {
            BlockDefinition *blockDef = chunk->blocks[ index ].blockDef;
            int visiable_block = blockDef->alpha != 0.0f;
            if ( visiable_block ) {
                int visiable_from_top = chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 1, z + 0 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_bottom = chunk->blocks[ chunk_get_index_from_coords( x + 0, y - 1, z + 0 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_left = chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, z - 1 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_right = chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, z + 1 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_front = chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 0, z + 0 ) ].blockDef->alpha < blockDef->alpha;
                int visiable_from_back = chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 0, z + 0 ) ].blockDef->alpha < blockDef->alpha;

                int block_could_be_visiable = visiable_from_top || visiable_from_bottom || visiable_from_left || visiable_from_right || visiable_from_front || visiable_from_back;

                if ( block_could_be_visiable ) {
                    BlockCoords *blockCoord;
                    int instance_index;
                    if ( blockDef->id == WATER ) {
                        blockCoord = &chunk->water.populated_blocks[ num_water_instances ];
                        num_water_instances += 1;
                    } else {
                        blockCoord = &chunk->solid.populated_blocks[ num_solid_instances ];
                        num_solid_instances += 1;
                    }
                    blockCoord->x = chunk->chunk_x * CHUNK_SIZE + x;
                    blockCoord->y = chunk->chunk_y * CHUNK_SIZE + y;
                    blockCoord->z = chunk->chunk_z * CHUNK_SIZE + z;
                    blockCoord->face_top = blockDef->textures.top - 1;
                    blockCoord->face_sides = blockDef->textures.side - 1;
                    blockCoord->face_bottom = blockDef->textures.bottom - 1;
                }
            }
        }
    }
    chunk->solid.num_instances = num_solid_instances;
    chunk->water.num_instances = num_water_instances;
}

void chunk_program_terrain( Chunk *chunk ) {
    if ( chunk - chunk->solid.num_instances != 0 ) {
        vertex_buffer_set_data( &chunk->solid.vb_coords, chunk->solid.populated_blocks, sizeof( BlockCoords ) * chunk->solid.num_instances );
        chunk->should_render = 1;
    }
    if ( chunk - chunk->water.num_instances != 0 ) {
        vertex_buffer_set_data( &chunk->water.vb_coords, chunk->water.populated_blocks, sizeof( BlockCoords ) * chunk->water.num_instances );
        chunk->should_render = 1;
    }
}

void chunk_unprogram_terrain( Chunk *chunk ) {
    chunk->should_render = 0;
}