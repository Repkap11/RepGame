#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"

void chunk_calculate_sides( Chunk *chunk, TRIP_ARGS( int center_next_ ) ) {
    unsigned int *chunk_ib_data_solid = ( unsigned int * )malloc( IB_SOLID_SIZE * sizeof( unsigned int ) );
    unsigned int *chunk_ib_data_water = ( unsigned int * )malloc( IB_WATER_SIZE * sizeof( unsigned int ) );

    int visable_top = chunk->chunk_y <= center_next_y;
    int visable_bottom = chunk->chunk_y >= center_next_y;
    int visable_right = chunk->chunk_x <= center_next_x;
    int visable_left = chunk->chunk_x >= center_next_x;
    int visable_front = chunk->chunk_z <= center_next_z;
    int visable_back = chunk->chunk_z >= center_next_z;

    int ib_size_solid = 0;
    int ib_size_water = 0;
    if ( visable_front ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 12 * FACE_FRONT + i ]; // bad
        }
    }
    if ( visable_right ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 12 * FACE_RIGHT + i ];
        }
    }
    if ( visable_back ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 12 * FACE_BACK + i ]; // bad
        }
    }
    if ( visable_left ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 12 * FACE_LEFT + i ]; // bad
        }
    }
    if ( visable_top ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 12 * FACE_TOP + i ];
            chunk_ib_data_water[ ib_size_water++ ] = ib_data_water[ 12 * IB_POSITION_WATER_TOP + i ];
        }
    }
    if ( visable_bottom ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_solid[ ib_size_solid++ ] = ib_data_solid[ 12 * FACE_BOTTOM + i ];
            chunk_ib_data_water[ ib_size_water++ ] = ib_data_water[ 12 * IB_POSITION_WATER_BOTTOM + i ];
        }
    }
    index_buffer_set_data( &chunk->solid.ib, chunk_ib_data_solid, ib_size_solid );
    index_buffer_set_data( &chunk->water.ib, chunk_ib_data_water, ib_size_water );
    free( chunk_ib_data_solid );
    free( chunk_ib_data_water );
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
        vertex_array_add_buffer( &chunk->solid.va, vb_block_solid, vbl_block, 0, 0 );
        vertex_array_add_buffer( &chunk->solid.va, &chunk->solid.vb_coords, vbl_coords, 1, vbl_block->current_size );
    }
    {
        index_buffer_init( &chunk->water.ib );
        vertex_buffer_init( &chunk->water.vb_coords );
        vertex_array_init( &chunk->water.va );
        vertex_array_add_buffer( &chunk->water.va, vb_block_water, vbl_block, 0, 0 );
        vertex_array_add_buffer( &chunk->water.va, &chunk->water.vb_coords, vbl_coords, 1, vbl_block->current_size );
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
        if ( LOAD_CHUNKDS_WITH_CUDA ) {
            map_gen_load_block_cuda( chunk );
        } else {
            map_gen_load_block_c( chunk );
        }
        chunk->ditry = PERSIST_ALL_CHUNKS;
    }
    chunk_calculate_popupated_blocks( chunk );
}

typedef struct {
    int can_be_seen;
    int visable;
    int has_been_drawn;
    int solid;
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];

} WorkingSpace;

int chunk_can_extend_rect( Chunk *chunk, BlockID rect_blockID, unsigned int *packed_lighting, WorkingSpace *workingSpace, TRIP_ARGS( int starting_ ), TRIP_ARGS( int size_ ), TRIP_ARGS( int dir_ ) ) {
    if ( DISABLE_GROUPING_BLOCKS ) {
        return 0;
    }
    if ( starting_x + size_x + dir_x > CHUNK_SIZE )
        return 0;
    if ( starting_y + size_y + dir_y > CHUNK_SIZE )
        return 0;
    if ( starting_z + size_z + dir_z > CHUNK_SIZE )
        return 0;
    if ( ( ( size_x > 1 ) + ( size_y > 1 ) + ( size_z > 1 ) ) == 3 ) {
        if ( !( size_x == 1 && size_y == 1 && size_z == 1 ) ) {
            pr_debug( "Error, there must be 2 values different in a plane" );
        }
    }
    int num_checked_blocks = 0;
    for ( int new_x = starting_x; new_x < starting_x + size_x; new_x++ ) {
        for ( int new_y = starting_y; new_y < starting_y + size_y; new_y++ ) {
            for ( int new_z = starting_z; new_z < starting_z + size_z; new_z++ ) {
                int index = chunk_get_index_from_coords( new_x + dir_x, new_y + dir_y, new_z + dir_z );
                BlockID new_blockID = chunk->blocks[ index ];
                num_checked_blocks++;
                if ( workingSpace[ index ].has_been_drawn ) {
                    return 0;
                }
                if ( !workingSpace[ index ].can_be_seen ) {
                    return 0;
                }
                if ( new_blockID != rect_blockID ) {
                    return 0;
                }
                for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                    if ( packed_lighting[ i ] != workingSpace[ index ].packed_lighting[ i ] ) {
                        return 0;
                    }
                }
            }
        }
    }
    int expected_checked_blocks = size_x * size_y * size_z;
    if ( num_checked_blocks != expected_checked_blocks ) {
        pr_debug( "Error, Didnt check enough blocks" );
    }
    return 1;
}

inline int min( int a, int b, int c, int d ) {
    int diag1 = ( a ) + ( d );
    int diag2 = ( b ) + ( c );
    int result = ( diag1 < diag2 ? diag1 : diag2 );
    return result;
}

void chunk_calculate_popupated_blocks( Chunk *chunk ) {
    int num_water_instances = 0;
    int num_solid_instances = 0;
    if ( chunk->solid.populated_blocks || chunk->water.populated_blocks ) {
        pr_debug( "Error, populated blocks already populated" );
    }

    chunk->solid.populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    chunk->water.populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    WorkingSpace *workingSpace = ( WorkingSpace * )calloc( CHUNK_BLOCK_SIZE, sizeof( WorkingSpace ) );

    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( drawn_block ) {
            BlockID blockID = chunk->blocks[ index ];
            Block *block = block_definition_get_definition( blockID );
            float alpha = block->alpha;
            int visiable_block = alpha != 0.0f;
            workingSpace[ index ].visable = visiable_block;
            workingSpace[ index ].solid = alpha == 1.0f;

            if ( visiable_block ) {
                // 1 Offset
                int xplus = x + 1;
                int xminus = x - 1;

                int yplus = y + 1;
                int yminus = y - 1;

                int zplus = z + 1;
                int zminus = z - 1;

                int top = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yplus, z + 0 ) ] )->alpha < alpha;
                int bottom = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yminus, z + 0 ) ] )->alpha < alpha;
                int front = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, zplus ) ] )->alpha < alpha;
                int back = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, zminus ) ] )->alpha < alpha;
                int right = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, y + 0, z + 0 ) ] )->alpha < alpha;
                int left = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, y + 0, z + 0 ) ] )->alpha < alpha;

                int block_could_be_visiable = top || bottom || left || right || front || back;
                workingSpace[ index ].can_be_seen = block_could_be_visiable;

                // 2 Offsets
                int tl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yplus, z + 0 ) ] )->alpha >= alpha;
                int tr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yplus, z + 0 ) ] )->alpha >= alpha;
                int tf = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yplus, zplus ) ] )->alpha >= alpha;
                int tba = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yplus, zminus ) ] )->alpha >= alpha;

                int bol = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yminus, z + 0 ) ] )->alpha >= alpha;
                int bor = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yminus, z + 0 ) ] )->alpha >= alpha;
                int bof = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yminus, zplus ) ] )->alpha >= alpha;
                int boba = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yminus, zminus ) ] )->alpha >= alpha;

                int fl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, y + 0, zplus ) ] )->alpha >= alpha;
                int bal = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, y + 0, zminus ) ] )->alpha >= alpha;
                int fr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, y + 0, zplus ) ] )->alpha >= alpha;
                int bar = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, y + 0, zminus ) ] )->alpha >= alpha;

                // 3 Offsetes
                int tfl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yplus, zplus ) ] )->alpha >= alpha;
                int tbl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yplus, zminus ) ] )->alpha >= alpha;
                int tfr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yplus, zplus ) ] )->alpha >= alpha;
                int tbr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yplus, zminus ) ] )->alpha >= alpha;

                int bfl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yminus, zplus ) ] )->alpha >= alpha;
                int bbl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yminus, zminus ) ] )->alpha >= alpha;
                int bfr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yminus, zplus ) ] )->alpha >= alpha;
                int bbr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yminus, zminus ) ] )->alpha >= alpha;
                {
                    int top_tfr = tf && tr ? 3 : ( tf + tr + tfr );
                    int top_tfl = tf && tl ? 3 : ( tf + tl + tfl );
                    int top_tbr = tba && tr ? 3 : ( tba + tr + tbr );
                    int top_tbl = tba && tl ? 3 : ( tba + tl + tbl );
                    int tcc = min( top_tfr, top_tfl, top_tbr, top_tbl );
                    workingSpace[ index ].packed_lighting[ FACE_TOP ] =                         //
                        ( ( top_tfr << CORNER_OFFSET_tfr ) | ( top_tfl << CORNER_OFFSET_tfl ) | //
                          ( top_tbr << CORNER_OFFSET_tbr ) | ( top_tbl << CORNER_OFFSET_tbl ) | //
                          tcc << CORNER_OFFSET_c );
                }
                {
                    int bottom_bfr = bof && bor ? 3 : ( bof + bor + bfr );
                    int bottom_bfl = bof && bol ? 3 : ( bof + bol + bfl );
                    int bottom_bbr = boba && bor ? 3 : ( boba + bor + bbr );
                    int bottom_bbl = boba && bol ? 3 : ( boba + bol + bbl );
                    int bcc = min( bottom_bfr, bottom_bfl, bottom_bbr, bottom_bbl );
                    workingSpace[ index ].packed_lighting[ FACE_BOTTOM ] =                            //
                        ( ( bottom_bfr << CORNER_OFFSET_bfr ) | ( bottom_bfl << CORNER_OFFSET_bfl ) | //
                          ( bottom_bbr << CORNER_OFFSET_bbr ) | ( bottom_bbl << CORNER_OFFSET_bbl ) | //
                          bcc << CORNER_OFFSET_c );
                }
                {
                    int front_tfr = tf && fr ? 3 : ( tf + fr + tfr );
                    int front_tfl = tf && fl ? 3 : ( tf + fl + tfl );
                    int front_bfr = bof && fr ? 3 : ( bof + fr + bfr );
                    int front_bfl = bof && fl ? 3 : ( bof + fl + bfl );
                    int cfc = min( front_tfr, front_tfl, front_bfr, front_bfl );
                    workingSpace[ index ].packed_lighting[ FACE_FRONT ] =                           //
                        ( ( front_tfr << CORNER_OFFSET_tfr ) | ( front_tfl << CORNER_OFFSET_tfl ) | //
                          ( front_bfr << CORNER_OFFSET_bfr ) | ( front_bfl << CORNER_OFFSET_bfl ) | //
                          cfc << CORNER_OFFSET_c );
                }
                {
                    int back_tbr = tba && bar ? 3 : ( tba + bar + tbr );
                    int back_tbl = tba && bal ? 3 : ( tba + bal + tbl );
                    int back_bbr = boba && bar ? 3 : ( boba + bar + bbr );
                    int back_bbl = boba && bal ? 3 : ( boba + bal + bbl );
                    int cbc = min( back_tbr, back_tbl, back_bbr, back_bbl );
                    workingSpace[ index ].packed_lighting[ FACE_BACK ] =                          //
                        ( ( back_tbr << CORNER_OFFSET_tbr ) | ( back_tbl << CORNER_OFFSET_tbl ) | //
                          ( back_bbr << CORNER_OFFSET_bbr ) | ( back_bbl << CORNER_OFFSET_bbl ) | //
                          cbc << CORNER_OFFSET_c );
                }
                {
                    int right_tfr = tr && fr ? 3 : ( tr + fr + tfr );
                    int right_tbr = tr && bar ? 3 : ( tr + bar + tbr );
                    int right_bfr = bor && fr ? 3 : ( bor + fr + bfr );
                    int right_bbr = bor && bar ? 3 : ( bor + bar + bbr );
                    int ccr = min( right_tfr, right_tbr, right_bfr, right_bbr );
                    workingSpace[ index ].packed_lighting[ FACE_RIGHT ] =                           //
                        ( ( right_tfr << CORNER_OFFSET_tfr ) | ( right_tbr << CORNER_OFFSET_tbr ) | //
                          ( right_bfr << CORNER_OFFSET_bfr ) | ( right_bbr << CORNER_OFFSET_bbr ) | //
                          ccr << CORNER_OFFSET_c );
                }
                {
                    int left_tfl = tl && fl ? 3 : ( tl + fl + tfl );
                    int left_tbl = tl && bal ? 3 : ( tl + bal + tbl );
                    int left_bfl = bol && fl ? 3 : ( bol + fl + bfl );
                    int left_bbl = bol && bal ? 3 : ( bol + bal + bbl );
                    int ccl = min( left_tfl, left_tbl, left_bfl, left_bbl );
                    workingSpace[ index ].packed_lighting[ FACE_LEFT ] =                          //
                        ( ( left_tfl << CORNER_OFFSET_tfl ) | ( left_tbl << CORNER_OFFSET_tbl ) | //
                          ( left_bfl << CORNER_OFFSET_bfl ) | ( left_bbl << CORNER_OFFSET_bbl ) | //
                          ccl << CORNER_OFFSET_c );
                }

            } else {
                workingSpace[ index ].can_be_seen = 0;
            }
        }
    }
    for ( int min_size = 64; min_size != 0; min_size = ( min_size == 1 ? 0 : min_size / 2 ) ) {
        for ( int y = 0; y < CHUNK_SIZE; y++ ) {
            for ( int x = 0; x < CHUNK_SIZE; x++ ) {
                for ( int z = 0; z < CHUNK_SIZE; z++ ) {
                    int index = chunk_get_index_from_coords( x, y, z );
                    if ( !workingSpace[ index ].has_been_drawn ) {

                        BlockID blockID = chunk->blocks[ index ];
                        Block *block = block_definition_get_definition( blockID );
                        int visiable_block = workingSpace[ index ].visable;
                        int can_be_seen = workingSpace[ index ].can_be_seen;
                        int has_been_drawn = workingSpace[ index ].has_been_drawn;
                        unsigned int *packed_lighting = workingSpace[ index ].packed_lighting;

                        if ( visiable_block && can_be_seen && !has_been_drawn ) {
                            int can_extend_x, can_extend_z, can_extend_y;

                            int size_x = 1;
                            int size_y = 1;
                            int size_z = 1;
                            do {
                                can_extend_x = chunk_can_extend_rect( chunk, blockID, packed_lighting, workingSpace, x + size_x - 1, y, z, 1, size_y, size_z, 1, 0, 0 );
                                if ( can_extend_x )
                                    size_x++;
                                can_extend_z = chunk_can_extend_rect( chunk, blockID, packed_lighting, workingSpace, x, y, z + size_z - 1, size_x, size_y, 1, 0, 0, 1 );
                                if ( can_extend_z )
                                    size_z++;
                                can_extend_y = chunk_can_extend_rect( chunk, blockID, packed_lighting, workingSpace, x, y + size_y - 1, z, size_x, 1, size_z, 0, 1, 0 );
                                if ( can_extend_y )
                                    size_y++;
                            } while ( can_extend_x || can_extend_z || can_extend_y );
                            int full_size = size_x * size_z * size_y;
                            if ( full_size >= min_size ) {
                                for ( int new_x = x; new_x < x + size_x; new_x++ ) {
                                    for ( int new_z = z; new_z < z + size_z; new_z++ ) {
                                        for ( int new_y = y; new_y < y + size_y; new_y++ ) {
                                            int index = chunk_get_index_from_coords( new_x, new_y, new_z );
                                            workingSpace[ index ].has_been_drawn = 1;
                                        }
                                    }
                                }
                                workingSpace[ index ].has_been_drawn = 1;

                                BlockCoords *blockCoord;
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

                                blockCoord->mesh_x = size_x;
                                blockCoord->mesh_y = size_y;
                                blockCoord->mesh_z = size_z;

                                for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                                    blockCoord->packed_lighting[ i ] = workingSpace[ index ].packed_lighting[ i ];
                                }

                                blockCoord->face_top = block->textures.top - 1;
                                blockCoord->face_sides = block->textures.side - 1;
                                blockCoord->face_bottom = block->textures.bottom - 1;
                            }
                        }
                    }
                }
            }
        }
    }

    free( workingSpace );

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