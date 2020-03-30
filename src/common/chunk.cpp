#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/structure_gen.hpp"

static unsigned int ib_data_flowers[] = {
    14, 0, 13, // Right, Back, Right
    13, 0, 14, //
    14, 0, 3,  // Right, Back, Back
    3,  0, 14, //

    7,  9, 4,  // Front, Right, Front
    4,  9, 7,  //
    7,  9, 10, // Front, Right, Right
    10, 9, 7,  //
};

void chunk_calculate_sides( Chunk *chunk, TRIP_ARGS( int center_next_ ) ) {
    unsigned int *chunk_ib_data[ LAST_RENDER_ORDER ];
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        chunk_ib_data[ renderOrder ] = ( unsigned int * )malloc( render_order_ib_size( ( RenderOrder )renderOrder ) * sizeof( unsigned int ) );
    }

    int visable_top = chunk->chunk_y <= center_next_y;
    int visable_bottom = chunk->chunk_y >= center_next_y;
    int visable_right = chunk->chunk_x <= center_next_x;
    int visable_left = chunk->chunk_x >= center_next_x;
    int visable_front = chunk->chunk_z <= center_next_z;
    int visable_back = chunk->chunk_z >= center_next_z;

    int ib_size[ LAST_RENDER_ORDER ] = {0};
    if ( visable_front ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_FRONT + i ];
        }
    }
    if ( visable_right ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_RIGHT + i ];
        }
    }
    if ( visable_back ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_BACK + i ];
        }
    }
    if ( visable_left ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_LEFT + i ];
        }
    }
    if ( visable_top ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_TOP + i ];
            chunk_ib_data[ RenderOrder_Water ][ ib_size[ RenderOrder_Water ]++ ] = ib_data_water[ 12 * IB_POSITION_WATER_TOP + i ];
        }
    }
    if ( visable_bottom ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_BOTTOM + i ];
            chunk_ib_data[ RenderOrder_Water ][ ib_size[ RenderOrder_Water ]++ ] = ib_data_water[ 12 * IB_POSITION_WATER_BOTTOM + i ];
        }
    }
    for ( int renderOrder = 1; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        unsigned int *ib_data = chunk_ib_data[ renderOrder ];
        unsigned int ib_new_size = ib_size[ renderOrder ];
        if ( renderOrder == RenderOrder_Flowers ) {
            ib_data = ib_data_flowers;
            ib_new_size = render_order_ib_size( ( RenderOrder )renderOrder );
        } else if ( renderOrder == RenderOrder_Opaque || renderOrder == RenderOrder_Water ) {
        } else {
            pr_debug( "Unexpected index buffer. Crash likely on WASM ro:%d", renderOrder );
        }
        if ( renderOrder == RenderOrder_Opaque ) {
            // pr_debug( "Should be %p:%d", ib_data, ib_new_size );
            // index_buffer_set_data( &chunk->layers[ renderOrder ].ib, ib_data_solid, IB_SOLID_SIZE );
            index_buffer_set_data( &chunk->layers[ renderOrder ].ib, ib_data, ib_new_size );

        } else {
            index_buffer_set_data( &chunk->layers[ renderOrder ].ib, ib_data, ib_new_size );
        }
        free( chunk_ib_data[ renderOrder ] );
    }
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
        chunk->blocks = ( BlockState * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockState ) );
    }

    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        VertexBuffer *vb;
        switch ( renderOrder ) {
            case RenderOrder_Water:
                vb = vb_block_water;
                break;
            default:
                vb = vb_block_solid;
        }
        index_buffer_init( &chunk->layers[ renderOrder ].ib );
        vertex_buffer_init( &chunk->layers[ renderOrder ].vb_coords );
        vertex_array_init( &chunk->layers[ renderOrder ].va );
        vertex_array_add_buffer( &chunk->layers[ renderOrder ].va, vb, vbl_block, 0, 0 );
        vertex_array_add_buffer( &chunk->layers[ renderOrder ].va, &chunk->layers[ renderOrder ].vb_coords, vbl_coords, 1, vbl_block->current_size );
    }
}

void chunk_destroy( Chunk *chunk ) {
    if ( REMEMBER_BLOCKS ) {
        free( chunk->blocks );
    }

    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        if ( chunk->layers[ renderOrder ].populated_blocks ) {
            free( chunk->layers[ renderOrder ].populated_blocks );
        }
        chunk->layers[ renderOrder ].populated_blocks = 0;
    }
}

void chunk_render( const Chunk *chunk, const Renderer *renderer, const Shader *shader, RenderOrder renderOrder ) {
    if ( chunk->should_render && chunk->layers[ renderOrder ].num_instances != 0 ) {
        if ( chunk->is_loading ) {
            pr_debug( "Error, attempting to render loading chunk" );
        }
        renderer_draw( renderer, &chunk->layers[ renderOrder ].va, &chunk->layers[ renderOrder ].ib, shader, chunk->layers[ renderOrder ].num_instances );
    }
}

BlockState chunk_get_block( Chunk *chunk, int x, int y, int z ) {
    if ( !REMEMBER_BLOCKS ) {
        return BLOCK_STATE_AIR;
    }
    if ( chunk->blocks == NULL ) {
        // pr_debug("Chunk has no blocks");
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    if ( x > CHUNK_SIZE + 1 || //
         y > CHUNK_SIZE + 1 || //
         z > CHUNK_SIZE + 1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    if ( x < -1 || //
         y < -1 || //
         z < -1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    return chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ];
}

void chunk_set_block( Chunk *chunk, int x, int y, int z, BlockState blockState ) {
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

    // Update the block in the client...
    chunk->blocks[ chunk_get_index_from_coords( x, y, z ) ] = blockState;
}

void chunk_persist( Chunk *chunk ) {
    if ( REMEMBER_BLOCKS ) {
        map_storage_persist( chunk );
    }
}

int use_cuda = 0;
void chunk_load_terrain( Chunk *chunk ) {
    if ( !REMEMBER_BLOCKS ) {
        chunk->blocks = ( BlockState * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockState ) );
    }
    // pr_debug( "Loading chunk terrain x:%d y:%d z:%d", chunk->chunk_x, chunk->chunk_y, chunk->chunk_z );
    int loaded = map_storage_load( chunk );
    if ( !loaded ) {
        // We havn't loaded this chunk before, map gen it.
        if ( LOAD_CHUNKDS_WITH_CUDA ) {
            map_gen_load_block_cuda( chunk );
        } else {
            map_gen_load_block_c( chunk );
        }
        structure_gen_place( chunk );
        chunk->dirty = PERSIST_ALL_CHUNKS;
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

int chunk_can_extend_rect( Chunk *chunk, BlockState blockState, unsigned int *packed_lighting, WorkingSpace *workingSpace, TRIP_ARGS( int starting_ ), TRIP_ARGS( int size_ ), TRIP_ARGS( int dir_ ) ) {
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
                num_checked_blocks++;
                if ( workingSpace[ index ].has_been_drawn ) {
                    return 0;
                }
                if ( !workingSpace[ index ].can_be_seen ) {
                    return 0;
                }
                BlockState new_blockState = chunk->blocks[ index ];
                if ( !BlockStates_equal( new_blockState, blockState ) ) {
                    return 0;
                }
                if ( new_blockState.rotation != blockState.rotation ) {
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

inline int get_rotated_face( int face, int rotation ) {
    int result = face;
    for ( int i = 0; i < rotation; i++ ) {
        result = FACE_ROTATE_90[ result ];
    }
    return result;
}

void chunk_calculate_popupated_blocks( Chunk *chunk ) {
    int num_instances[ LAST_RENDER_ORDER ] = {0};

    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        if ( chunk->layers[ renderOrder ].populated_blocks ) {
            pr_debug( "Error, populated blocks already populated" );
        }
        chunk->layers[ renderOrder ].populated_blocks = ( BlockCoords * )calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) );
    }
    WorkingSpace *workingSpace = ( WorkingSpace * )calloc( CHUNK_BLOCK_SIZE, sizeof( WorkingSpace ) );

    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        int drawn_block = chunk_get_coords_from_index( index, &x, &y, &z );
        if ( drawn_block ) {
            BlockID blockID = chunk->blocks[ index ].id;
            Block *block = block_definition_get_definition( blockID );

            workingSpace[ index ].visable = render_order_is_visible( block->renderOrder );

            int visible_from[ NUM_FACES_IN_CUBE ] = {0, 0, 0, 0, 0, 0};
            int block_is_visiable = 0;
            if ( workingSpace[ index ].visable ) {
                for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
                    BlockState block_next_to_state = chunk->blocks[ chunk_get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face ], y + FACE_DIR_Y_OFFSETS[ face ], z + FACE_DIR_Z_OFFSETS[ face ] ) ];
                    // TODO look at the blocks rotation...
                    Block *block_next_to = block_definition_get_definition( block_next_to_state.id );

                    int opposing_face = get_rotated_face( face, block_next_to_state.rotation );
                    opposing_face = OPPOSITE_FACE[ opposing_face ];

                    bool is_seethrough = block->calculated.is_seethrough[ face ];
                    if ( is_seethrough && block->hides_self ) { // water and glass
                        if ( face == FACE_TOP && block->renderOrder == RenderOrder_Water ) {
                            visible_from[ face ] = block_next_to->id != block->id;
                        } else {
                            visible_from[ face ] = block_next_to->calculated.is_seethrough[ opposing_face ] && block_next_to_state.id != blockID;
                        }
                    } else if ( is_seethrough ) { // The current block is seethough on this face. Check all the directions not opposing it;
                        bool visable_through_opposing = false;
                        for ( int face_around = FACE_TOP; face_around < NUM_FACES_IN_CUBE; face_around++ ) {
                            if ( face_around == opposing_face ) {
                                // It's the non_opposing_face and parallel with this face, don't count it
                                continue;
                            }
                            BlockState block_around_state = chunk->blocks[ chunk_get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face_around ], y + FACE_DIR_Y_OFFSETS[ face_around ], z + FACE_DIR_Z_OFFSETS[ face_around ] ) ];
                            Block *block_around = block_definition_get_definition( block_around_state.id );
                            int around_face = get_rotated_face( face_around, block_around_state.rotation );
                            around_face = OPPOSITE_FACE[ around_face ];
                            bool visible_towards_me = block_around->calculated.is_seethrough[ around_face ];
                            if ( visible_towards_me ) {
                                visable_through_opposing = true;
                                break;
                            }
                        }
                        visible_from[ face ] = visable_through_opposing;

                    } else {
                        bool visable_through_opposing = block_next_to->calculated.is_seethrough[ opposing_face ];
                        visible_from[ face ] = visable_through_opposing;
                    }
                    block_is_visiable |= visible_from[ face ];
                }
                int can_be_shaded = render_order_can_be_shaded( block->renderOrder );
                // 1 Offset
                int xplus = x + 1;
                int xminus = x - 1;
                int yplus = y + 1;
                int yminus = y - 1;
                int zplus = z + 1;
                int zminus = z - 1;

                int t = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yplus, z + 0 ) ].id )->casts_shadow;
                int bo = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yminus, z + 0 ) ].id )->casts_shadow;
                int f = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, zplus ) ].id )->casts_shadow;
                int ba = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, y + 0, zminus ) ].id )->casts_shadow;
                int r = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, y + 0, z + 0 ) ].id )->casts_shadow;
                int l = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, y + 0, z + 0 ) ].id )->casts_shadow;

                // If the block is visible and can be shaded, check neighbors for shade and populated the packed lighting
                // 2 Offsets
                int tl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yplus, z + 0 ) ].id )->casts_shadow;
                int tr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yplus, z + 0 ) ].id )->casts_shadow;
                int tf = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yplus, zplus ) ].id )->casts_shadow;
                int tba = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yplus, zminus ) ].id )->casts_shadow;

                int bol = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yminus, z + 0 ) ].id )->casts_shadow;
                int bor = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yminus, z + 0 ) ].id )->casts_shadow;
                int bof = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yminus, zplus ) ].id )->casts_shadow;
                int boba = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( x + 0, yminus, zminus ) ].id )->casts_shadow;

                int fl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, y + 0, zplus ) ].id )->casts_shadow;
                int bal = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, y + 0, zminus ) ].id )->casts_shadow;
                int fr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, y + 0, zplus ) ].id )->casts_shadow;
                int bar = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, y + 0, zminus ) ].id )->casts_shadow;

                // 3 Offsetes
                int tfl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yplus, zplus ) ].id )->casts_shadow;
                int tbl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yplus, zminus ) ].id )->casts_shadow;
                int tfr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yplus, zplus ) ].id )->casts_shadow;
                int tbr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yplus, zminus ) ].id )->casts_shadow;

                int bfl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yminus, zplus ) ].id )->casts_shadow;
                int bbl = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xminus, yminus, zminus ) ].id )->casts_shadow;
                int bfr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yminus, zplus ) ].id )->casts_shadow;
                int bbr = block_definition_get_definition( chunk->blocks[ chunk_get_index_from_coords( xplus, yminus, zminus ) ].id )->casts_shadow;
                if ( visible_from[ FACE_TOP ] && can_be_shaded ) {
                    int top_tfr = ( tf && tr ? 3 : ( tf + tr + tfr ) ) + t;
                    int top_tfl = ( tf && tl ? 3 : ( tf + tl + tfl ) ) + t;
                    int top_tbr = ( tba && tr ? 3 : ( tba + tr + tbr ) ) + t;
                    int top_tbl = ( tba && tl ? 3 : ( tba + tl + tbl ) ) + t;
                    top_tfr = top_tfr > 3 ? 3 : top_tfr;
                    top_tfl = top_tfl > 3 ? 3 : top_tfl;
                    top_tbr = top_tbr > 3 ? 3 : top_tbr;
                    top_tbl = top_tbl > 3 ? 3 : top_tbl;
                    int tcc = min( top_tfr, top_tfl, top_tbr, top_tbl );
                    workingSpace[ index ].packed_lighting[ FACE_TOP ] =                         //
                        ( ( top_tfr << CORNER_OFFSET_tfr ) | ( top_tfl << CORNER_OFFSET_tfl ) | //
                          ( top_tbr << CORNER_OFFSET_tbr ) | ( top_tbl << CORNER_OFFSET_tbl ) | //
                          tcc << CORNER_OFFSET_c );
                } else {
                    workingSpace[ index ].packed_lighting[ FACE_TOP ] = block->no_light;
                }
                if ( visible_from[ FACE_BOTTOM ] && can_be_shaded ) {
                    int bottom_bfr = ( bof && bor ? 3 : ( bof + bor + bfr ) ) + bo;
                    int bottom_bfl = ( bof && bol ? 3 : ( bof + bol + bfl ) ) + bo;
                    int bottom_bbr = ( boba && bor ? 3 : ( boba + bor + bbr ) ) + bo;
                    int bottom_bbl = ( boba && bol ? 3 : ( boba + bol + bbl ) ) + bo;
                    bottom_bfr = bottom_bfr > 3 ? 3 : bottom_bfr;
                    bottom_bfl = bottom_bfl > 3 ? 3 : bottom_bfl;
                    bottom_bbr = bottom_bbr > 3 ? 3 : bottom_bbr;
                    bottom_bbl = bottom_bbl > 3 ? 3 : bottom_bbl;
                    int bcc = min( bottom_bfr, bottom_bfl, bottom_bbr, bottom_bbl );
                    workingSpace[ index ].packed_lighting[ FACE_BOTTOM ] =                            //
                        ( ( bottom_bfr << CORNER_OFFSET_bfr ) | ( bottom_bfl << CORNER_OFFSET_bfl ) | //
                          ( bottom_bbr << CORNER_OFFSET_bbr ) | ( bottom_bbl << CORNER_OFFSET_bbl ) | //
                          bcc << CORNER_OFFSET_c );
                } else {
                    workingSpace[ index ].packed_lighting[ FACE_BOTTOM ] = block->no_light;
                }
                if ( visible_from[ FACE_FRONT ] && can_be_shaded ) {
                    int front_tfr = ( tf && fr ? 3 : ( tf + fr + tfr ) ) + f;
                    int front_tfl = ( tf && fl ? 3 : ( tf + fl + tfl ) ) + f;
                    int front_bfr = ( bof && fr ? 3 : ( bof + fr + bfr ) ) + f;
                    int front_bfl = ( bof && fl ? 3 : ( bof + fl + bfl ) ) + f;
                    front_tfr = front_tfr > 3 ? 3 : front_tfr;
                    front_tfl = front_tfl > 3 ? 3 : front_tfl;
                    front_bfr = front_bfr > 3 ? 3 : front_bfr;
                    front_bfl = front_bfl > 3 ? 3 : front_bfl;
                    int cfc = min( front_tfr, front_tfl, front_bfr, front_bfl );
                    workingSpace[ index ].packed_lighting[ FACE_FRONT ] =                           //
                        ( ( front_tfr << CORNER_OFFSET_tfr ) | ( front_tfl << CORNER_OFFSET_tfl ) | //
                          ( front_bfr << CORNER_OFFSET_bfr ) | ( front_bfl << CORNER_OFFSET_bfl ) | //
                          cfc << CORNER_OFFSET_c );
                } else {
                    workingSpace[ index ].packed_lighting[ FACE_FRONT ] = block->no_light;
                }
                if ( visible_from[ FACE_BACK ] && can_be_shaded ) {
                    int back_tbr = ( tba && bar ? 3 : ( tba + bar + tbr ) ) + ba;
                    int back_tbl = ( tba && bal ? 3 : ( tba + bal + tbl ) ) + ba;
                    int back_bbr = ( boba && bar ? 3 : ( boba + bar + bbr ) ) + ba;
                    int back_bbl = ( boba && bal ? 3 : ( boba + bal + bbl ) ) + ba;
                    back_tbr = back_tbr > 3 ? 3 : back_tbr;
                    back_tbl = back_tbl > 3 ? 3 : back_tbl;
                    back_bbr = back_bbr > 3 ? 3 : back_bbr;
                    back_bbl = back_bbl > 3 ? 3 : back_bbl;
                    int cbc = min( back_tbr, back_tbl, back_bbr, back_bbl );
                    workingSpace[ index ].packed_lighting[ FACE_BACK ] =                          //
                        ( ( back_tbr << CORNER_OFFSET_tbr ) | ( back_tbl << CORNER_OFFSET_tbl ) | //
                          ( back_bbr << CORNER_OFFSET_bbr ) | ( back_bbl << CORNER_OFFSET_bbl ) | //
                          cbc << CORNER_OFFSET_c );
                } else {
                    workingSpace[ index ].packed_lighting[ FACE_BACK ] = block->no_light;
                }
                if ( visible_from[ FACE_RIGHT ] && can_be_shaded ) {
                    int right_tfr = ( tr && fr ? 3 : ( tr + fr + tfr ) ) + r;
                    int right_tbr = ( tr && bar ? 3 : ( tr + bar + tbr ) ) + r;
                    int right_bfr = ( bor && fr ? 3 : ( bor + fr + bfr ) ) + r;
                    int right_bbr = ( bor && bar ? 3 : ( bor + bar + bbr ) ) + r;
                    right_tfr = right_tfr > 3 ? 3 : right_tfr;
                    right_tbr = right_tbr > 3 ? 3 : right_tbr;
                    right_bfr = right_bfr > 3 ? 3 : right_bfr;
                    right_bbr = right_bbr > 3 ? 3 : right_bbr;
                    int ccr = min( right_tfr, right_tbr, right_bfr, right_bbr );
                    workingSpace[ index ].packed_lighting[ FACE_RIGHT ] =                           //
                        ( ( right_tfr << CORNER_OFFSET_tfr ) | ( right_tbr << CORNER_OFFSET_tbr ) | //
                          ( right_bfr << CORNER_OFFSET_bfr ) | ( right_bbr << CORNER_OFFSET_bbr ) | //
                          ccr << CORNER_OFFSET_c );
                } else {
                    workingSpace[ index ].packed_lighting[ FACE_RIGHT ] = block->no_light;
                }
                if ( visible_from[ FACE_LEFT ] && can_be_shaded ) {
                    int left_tfl = ( tl && fl ? 3 : ( tl + fl + tfl ) ) + l;
                    int left_tbl = ( tl && bal ? 3 : ( tl + bal + tbl ) ) + l;
                    int left_bfl = ( bol && fl ? 3 : ( bol + fl + bfl ) ) + l;
                    int left_bbl = ( bol && bal ? 3 : ( bol + bal + bbl ) ) + l;
                    left_tfl = left_tfl > 3 ? 3 : left_tfl;
                    left_tbl = left_tbl > 3 ? 3 : left_tbl;
                    left_bfl = left_bfl > 3 ? 3 : left_bfl;
                    left_bbl = left_bbl > 3 ? 3 : left_bbl;
                    int ccl = min( left_tfl, left_tbl, left_bfl, left_bbl );
                    workingSpace[ index ].packed_lighting[ FACE_LEFT ] =                          //
                        ( ( left_tfl << CORNER_OFFSET_tfl ) | ( left_tbl << CORNER_OFFSET_tbl ) | //
                          ( left_bfl << CORNER_OFFSET_bfl ) | ( left_bbl << CORNER_OFFSET_bbl ) | //
                          ccl << CORNER_OFFSET_c );
                } else {
                    workingSpace[ index ].packed_lighting[ FACE_LEFT ] = block->no_light;
                }
                workingSpace[ index ].can_be_seen = block_is_visiable;
            } else {
                workingSpace[ index ].can_be_seen = 0;
            }
        }
    }
    for ( int y = 0; y < CHUNK_SIZE; y++ ) {
        for ( int x = 0; x < CHUNK_SIZE; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE; z++ ) {
                int index = chunk_get_index_from_coords( x, y, z );
                if ( !workingSpace[ index ].has_been_drawn ) {

                    BlockState blockState = chunk->blocks[ index ];
                    Block *block = block_definition_get_definition( blockState.id );
                    int visiable_block = workingSpace[ index ].visable;
                    int can_be_seen = workingSpace[ index ].can_be_seen;
                    int has_been_drawn = workingSpace[ index ].has_been_drawn;
                    unsigned int *packed_lighting = workingSpace[ index ].packed_lighting;

                    if ( visiable_block && can_be_seen && !has_been_drawn ) {

                        int size_x = 1;
                        int size_y = 1;
                        int size_z = 1;
                        int can_extend_x = 0;
                        int can_extend_y = 0;
                        int can_extend_z = 0;
                        do {
                            if ( block->calculated.can_mesh_x ) {
                                can_extend_x = chunk_can_extend_rect( chunk, blockState, packed_lighting, workingSpace, x + size_x - 1, y, z, 1, size_y, size_z, 1, 0, 0 );
                                if ( can_extend_x ) {
                                    size_x++;
                                }
                            }
                            if ( block->calculated.can_mesh_z ) {
                                can_extend_z = chunk_can_extend_rect( chunk, blockState, packed_lighting, workingSpace, x, y, z + size_z - 1, size_x, size_y, 1, 0, 0, 1 );
                                if ( can_extend_z ) {
                                    size_z++;
                                }
                            }
                            if ( block->calculated.can_mesh_y ) {
                                can_extend_y = chunk_can_extend_rect( chunk, blockState, packed_lighting, workingSpace, x, y + size_y - 1, z, size_x, 1, size_z, 0, 1, 0 );
                                if ( can_extend_y ) {
                                    size_y++;
                                }
                            }
                        } while ( can_extend_x || can_extend_z || can_extend_y );

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

                        blockCoord = &chunk->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                        num_instances[ block->renderOrder ] += 1;

                        blockCoord->x = chunk->chunk_x * CHUNK_SIZE + x;
                        blockCoord->y = chunk->chunk_y * CHUNK_SIZE + y;
                        blockCoord->z = chunk->chunk_z * CHUNK_SIZE + z;

                        blockCoord->mesh_x = size_x;
                        blockCoord->mesh_y = size_y;
                        blockCoord->mesh_z = size_z;
                        blockCoord->face_shift = blockState.rotation;
                        blockCoord->scale_x = PIXEL_TO_FLOAT( block->scale.x );
                        blockCoord->scale_y = PIXEL_TO_FLOAT( block->scale.y );
                        blockCoord->scale_z = PIXEL_TO_FLOAT( block->scale.z );

                        blockCoord->offset_x = PIXEL_TO_FLOAT( block->offset.x );
                        blockCoord->offset_y = PIXEL_TO_FLOAT( block->offset.y );
                        blockCoord->offset_z = PIXEL_TO_FLOAT( block->offset.z );

                        blockCoord->tex_offset_x = PIXEL_TO_FLOAT( block->tex_offset.x );
                        blockCoord->tex_offset_y = PIXEL_TO_FLOAT( block->tex_offset.y );
                        blockCoord->tex_offset_z = PIXEL_TO_FLOAT( block->tex_offset.z );

                        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                            blockCoord->packed_lighting[ i ] = workingSpace[ index ].packed_lighting[ i ];
                            blockCoord->face[ i ] = block->textures[ i ];
                        }
                        block_adjust_coord_based_on_state( block, &blockState, blockCoord );
                        // They are offset by 1 in the shader...
                        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
                            blockCoord->face[ i ] = blockCoord->face[ i ] - 1;
                        }
                    }
                }
            }
        }
    }
    free( workingSpace );

    if ( !REMEMBER_BLOCKS ) {
        if ( PERSIST_ALL_CHUNKS ) {
            map_storage_persist( chunk );
        }
        free( chunk->blocks );
    }
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        chunk->layers[ renderOrder ].num_instances = num_instances[ renderOrder ];
    }
}

void chunk_program_terrain( Chunk *chunk ) {
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        if ( chunk - chunk->layers[ renderOrder ].num_instances != 0 ) {
            vertex_buffer_set_data( &chunk->layers[ renderOrder ].vb_coords, chunk->layers[ renderOrder ].populated_blocks, sizeof( BlockCoords ) * chunk->layers[ renderOrder ].num_instances );
            chunk->should_render = 1;
        }
        free( chunk->layers[ renderOrder ].populated_blocks );
        chunk->layers[ renderOrder ].populated_blocks = 0;
    }
}

void chunk_unprogram_terrain( Chunk *chunk ) {
    chunk->should_render = 0;
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        if ( chunk->layers[ renderOrder ].populated_blocks ) {
            free( chunk->layers[ renderOrder ].populated_blocks );
        }
        chunk->layers[ renderOrder ].populated_blocks = 0;
    }
}