#include "RepGame.h"
#include "chunk.h"
#include "map_gen.h"
#include "map_storage.h"

void chunk_calculate_sides( Chunk *chunk, TRIP_ARGS( int center_next_ ) ) {
    unsigned int *chunk_ib_data_solid = ( unsigned int * )malloc( 3 * 2 * 6 * sizeof( unsigned int ) );
    unsigned int *chunk_ib_data_water = ( unsigned int * )malloc( 3 * 2 * 2 * sizeof( unsigned int ) );

    int visable_top = chunk->chunk_y <= center_next_y;
    int visable_bottom = chunk->chunk_y >= center_next_y;
    int visable_left = chunk->chunk_z <= center_next_z;
    int visable_right = chunk->chunk_z >= center_next_z;
    int visable_front = chunk->chunk_x <= center_next_x;
    int visable_back = chunk->chunk_x >= center_next_x;

    int ib_size_solid = 0;
    int ib_size_water = 0;
    if ( visable_front ) {
        for ( int i = 0; i < 6; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 6 * FACE_FRONT + i ];
        }
    }
    if ( visable_right ) {
        for ( int i = 0; i < 6; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 6 * FACE_RIGHT + i ];
        }
    }
    if ( visable_back ) {
        for ( int i = 0; i < 6; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 6 * FACE_BACK + i ];
        }
    }
    if ( visable_left ) {
        for ( int i = 0; i < 6; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 6 * FACE_LEFT + i ];
        }
    }
    if ( visable_top ) {
        for ( int i = 0; i < 6; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 6 * FACE_TOP + i ];
            chunk_ib_data_water[ ib_size_water++ ] = ib_data_water[ 6 * IB_POSITION_WATER_TOP + i ];
        }
    }
    if ( visable_bottom ) {
        for ( int i = 0; i < 6; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 6 * FACE_BOTTOM + i ];
            chunk_ib_data_water[ ib_size_water++ ] = ib_data_water[ 6 * IB_POSITION_WATER_BOTTOM + i ];
        }
    }
    index_buffer_set_data( &chunk->solid.ib, chunk_ib_data_solid, ib_size_solid );
    index_buffer_set_data( &chunk->water.ib, chunk_ib_data_water, ib_size_water );
    free( chunk_ib_data_solid );
    free( chunk_ib_data_water );
}

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

void chunk_init( Chunk *chunk, VertexBuffer *vb_block_solid, VertexBuffer *vb_block_water, VertexBufferLayout *vbl_block, VertexBufferLayout *vbl_coords ) {
    if ( REMEMBER_BLOCKS ) {
        chunk->blocks = ( BlockID * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockID ) );
    }

    {
        index_buffer_init( &chunk->solid.ib );
        vertex_buffer_init( &chunk->solid.vb_coords );
        vertex_array_init( &chunk->solid.va );
        vertex_array_add_buffer( &chunk->solid.va, vb_block_solid, vbl_block, 0 );
        vertex_array_add_buffer( &chunk->solid.va, &chunk->solid.vb_coords, vbl_coords, 1 );
    }
    {
        index_buffer_init( &chunk->water.ib );
        vertex_buffer_init( &chunk->water.vb_coords );
        vertex_array_init( &chunk->water.va );
        vertex_array_add_buffer( &chunk->water.va, vb_block_water, vbl_block, 0 );
        vertex_array_add_buffer( &chunk->water.va, &chunk->water.vb_coords, vbl_coords, 1 );
    }
}

void chunk_destroy( Chunk *chunk ) {
    if ( REMEMBER_BLOCKS ) {
        free( chunk->blocks );
    }
    if ( chunk->solid.populated_blocks ) {
        free( chunk->solid.populated_blocks );
    }
    chunk->solid.populated_blocks = 0;
    if ( chunk->water.populated_blocks ) {
        free( chunk->water.populated_blocks );
    }
    chunk->water.populated_blocks = 0;
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

BlockID chunk_get_block( Chunk *chunk, int x, int y, int z ) {
    if ( chunk->blocks == NULL ) {
        // pr_debug("Chunk has no blocks");
        return LAST_BLOCK_ID;
    }
    if ( x > CHUNK_SIZE + 1 || //
         y > CHUNK_SIZE + 1 || //
         z > CHUNK_SIZE + 1 ) {
        return LAST_BLOCK_ID;
    }
    if ( x < -1 || //
         y < -1 || //
         z < -1 ) {
        return LAST_BLOCK_ID;
    }
    return chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockID blockID ) {
    if ( chunk->blocks == NULL ) {
        // pr_debug("Chunk has no blocks");
        return;
    }
    if ( x > CHUNK_SIZE + 1 || //
         y > CHUNK_SIZE + 1 || //
         z > CHUNK_SIZE + 1 ) {
        return;
    }
    if ( x < -1 || //
         y < -1 || //
         z < -1 ) {
        return;
    }
    chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ] = blockID;
}

void chunk_persist( Chunk *chunk ) {
#ifdef REPGAME_LINUX
    if ( REMEMBER_BLOCKS ) {
        map_storage_persist( chunk );
    }
#endif
}

int use_cuda = 0;
void chunk_load_terrain( Chunk *chunk ) {
    if ( !REMEMBER_BLOCKS ) {
        chunk->blocks = ( BlockID * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockID ) );
    }
// pr_debug( "Loading chunk terrain x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
#ifdef REPGAME_LINUX
    int loaded = map_storage_load( chunk );
#else
    int loaded = 0;
#endif
    if ( !loaded ) {
        // We havn't loaded this chunk before, map gen it.
        if ( LOAD_WITH_CUDA ) {
            map_gen_load_block_cuda( chunk );
        } else {
            map_gen_load_block_c( chunk );
        }
        chunk->ditry = PERSIST_ALL_CHUNKS;
    }
    chunk_calculate_popupated_blocks( chunk );
}

void chunk_calculate_popupated_blocks( Chunk *chunk ) {
    int num_water_instances = 0;
    int num_solid_instances = 0;
    if ( chunk->solid.populated_blocks || chunk->water.populated_blocks ) {
        pr_debug( "Error, populated blocks already populated" );
    }

    chunk->solid.populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    chunk->water.populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( drawn_block ) {
            BlockID blockID = chunk->blocks[ index ];
            Block *block = block_definition_get_definition( blockID );
            float alpha = block->alpha;
            int visiable_block = alpha != 0.0f;
            if ( visiable_block ) {
                int visiable_from_top = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 1, z + 0 ) ] )->alpha < alpha;
                int visiable_from_bottom = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y - 1, z + 0 ) ] )->alpha < alpha;
                int visiable_from_left = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, z - 1 ) ] )->alpha < alpha;
                int visiable_from_right = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, z + 1 ) ] )->alpha < alpha;
                int visiable_from_front = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 1, y + 0, z + 0 ) ] )->alpha < alpha;
                int visiable_from_back = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x - 1, y + 0, z + 0 ) ] )->alpha < alpha;

                int block_could_be_visiable = visiable_from_top || visiable_from_bottom || visiable_from_left || visiable_from_right || visiable_from_front || visiable_from_back;

                if ( block_could_be_visiable ) {
                    BlockCoords *blockCoord;
                    int instance_index;
                    if ( blockID == WATER ) {
                        blockCoord = &chunk->water.populated_blocks[ num_water_instances ];
                        num_water_instances += 1;
                    } else {
                        blockCoord = &chunk->solid.populated_blocks[ num_solid_instances ];
                        num_solid_instances += 1;
                    }
                    blockCoord->x = chunk->chunk_x * CHUNK_SIZE + x;
                    blockCoord->y = chunk->chunk_y * CHUNK_SIZE + y;
                    blockCoord->z = chunk->chunk_z * CHUNK_SIZE + z;
                    blockCoord->face_top = block->textures.top - 1;
                    blockCoord->face_sides = block->textures.side - 1;
                    blockCoord->face_bottom = block->textures.bottom - 1;
                }
            }
        }
    }
    if ( !REMEMBER_BLOCKS ) {
#ifdef REPGAME_LINUX
        if ( PERSIST_ALL_CHUNKS ) {
            map_storage_persist( chunk );
        }
#endif
        free( chunk->blocks );
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
    free( chunk->solid.populated_blocks );
    free( chunk->water.populated_blocks );
    chunk->solid.populated_blocks = 0;
    chunk->water.populated_blocks = 0;
}

void chunk_unprogram_terrain( Chunk *chunk ) {
    chunk->should_render = 0;
    if ( chunk->solid.populated_blocks ) {
        free( chunk->solid.populated_blocks );
    }
    chunk->solid.populated_blocks = 0;
    if ( chunk->water.populated_blocks ) {
        free( chunk->water.populated_blocks );
    }
    chunk->water.populated_blocks = 0;
}