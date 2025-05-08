#include "common/RepGame.hpp"
#include "common/chunk.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/structure_gen.hpp"

constexpr static unsigned int ib_data_flowers[] = {
    14, 0, 13, // Right, Back, Right
    13, 0, 14, //
    14, 0, 3,  // Right, Back, Back
    3,  0, 14, //

    7,  9, 4,  // Front, Right, Front
    4,  9, 7,  //
    7,  9, 10, // Front, Right, Right
    10, 9, 7,  //
};

void Chunk::calculate_sides( const glm::ivec3 &center_next ) {
    unsigned int *chunk_ib_data[ LAST_RENDER_ORDER ];
    unsigned int *chunk_ib_data_reflect[ LAST_RENDER_ORDER ];
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        chunk_ib_data[ renderOrder ] = static_cast<unsigned int *>( malloc( render_order_ib_size( static_cast<RenderOrder>( renderOrder ) ) * sizeof( unsigned int ) ) );
        chunk_ib_data_reflect[ renderOrder ] = static_cast<unsigned int *>( malloc( render_order_ib_size( static_cast<RenderOrder>( renderOrder ) ) * sizeof( unsigned int ) ) );
    }
    int ib_size[ LAST_RENDER_ORDER ] = { 0 };

    for ( int i = 0; i < 12; i++ ) {
        chunk_ib_data_reflect[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ] ] = ib_data_solid[ 12 * FACE_FRONT + i ];
        chunk_ib_data[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ]++ ] = ib_data_solid[ 12 * FACE_FRONT + i ];
    }
    for ( int i = 0; i < 12; i++ ) {
        chunk_ib_data_reflect[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ] ] = ib_data_solid[ 12 * FACE_RIGHT + i ];
        chunk_ib_data[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ]++ ] = ib_data_solid[ 12 * FACE_RIGHT + i ];
    }
    for ( int i = 0; i < 12; i++ ) {
        chunk_ib_data_reflect[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ] ] = ib_data_solid[ 12 * FACE_BACK + i ];
        chunk_ib_data[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ]++ ] = ib_data_solid[ 12 * FACE_BACK + i ];
    }
    for ( int i = 0; i < 12; i++ ) {
        chunk_ib_data_reflect[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ] ] = ib_data_solid[ 12 * FACE_LEFT + i ];
        chunk_ib_data[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ]++ ] = ib_data_solid[ 12 * FACE_LEFT + i ];
    }
    for ( int i = 0; i < 12; i++ ) {
        chunk_ib_data_reflect[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ] ] = ib_data_solid[ 12 * FACE_BOTTOM + i ];
        chunk_ib_data[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ]++ ] = ib_data_solid[ 12 * FACE_TOP + i ];
    }
    for ( int i = 0; i < 12; i++ ) {
        chunk_ib_data_reflect[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ] ] = ib_data_solid[ 12 * FACE_TOP + i ];
        chunk_ib_data[ RenderOrder_Translucent ][ ib_size[ RenderOrder_Translucent ]++ ] = ib_data_solid[ 12 * FACE_BOTTOM + i ];
    }

    const bool visible_top = this->chunk_pos.y <= center_next.y;
    const bool visible_bottom = this->chunk_pos.y >= center_next.y;
    const bool visible_right = this->chunk_pos.x <= center_next.x;
    const bool visible_left = this->chunk_pos.x >= center_next.x;
    const bool visible_front = this->chunk_pos.z <= center_next.z;
    const bool visible_back = this->chunk_pos.z >= center_next.z;

    if ( visible_front ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_reflect[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ] ] = ib_data_solid[ 12 * FACE_FRONT + i ];
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_FRONT + i ];
        }
    }
    if ( visible_right ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_reflect[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ] ] = ib_data_solid[ 12 * FACE_RIGHT + i ];
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_RIGHT + i ];
        }
    }
    if ( visible_back ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_reflect[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ] ] = ib_data_solid[ 12 * FACE_BACK + i ];
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_BACK + i ];
        }
    }
    if ( visible_left ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_reflect[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ] ] = ib_data_solid[ 12 * FACE_LEFT + i ];
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_LEFT + i ];
        }
    }
    if ( visible_top ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_reflect[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ] ] = ib_data_solid[ 12 * FACE_BOTTOM + i ];
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_TOP + i ];
            chunk_ib_data[ RenderOrder_Water ][ ib_size[ RenderOrder_Water ]++ ] = ib_data_water[ 12 * IB_POSITION_WATER_TOP + i ];
        }
    }
    if ( visible_bottom ) {
        for ( int i = 0; i < 12; i++ ) {
            chunk_ib_data_reflect[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ] ] = ib_data_solid[ 12 * FACE_TOP + i ];
            chunk_ib_data[ RenderOrder_Opaque ][ ib_size[ RenderOrder_Opaque ]++ ] = ib_data_solid[ 12 * FACE_BOTTOM + i ];
            chunk_ib_data[ RenderOrder_Water ][ ib_size[ RenderOrder_Water ]++ ] = ib_data_water[ 12 * IB_POSITION_WATER_BOTTOM + i ];
        }
    }

    for ( int renderOrder = 1; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        const unsigned int *ib_data = chunk_ib_data[ renderOrder ];
        const unsigned int *ib_data_reflect = chunk_ib_data_reflect[ renderOrder ];
        unsigned int ib_new_size = ib_size[ renderOrder ];
        if ( renderOrder == RenderOrder_Flowers ) {
            ib_data = ib_data_flowers;
            ib_data_reflect = ib_data_flowers;
            ib_new_size = render_order_ib_size( static_cast<RenderOrder>( renderOrder ) );
        } else if ( renderOrder == RenderOrder_Opaque || renderOrder == RenderOrder_Water || renderOrder == RenderOrder_Translucent ) {
        } else {
            pr_debug( "Unexpected index buffer. Crash likely on WASM ro:%d", renderOrder );
        }
        this->layers[ renderOrder ].ib.set_data( ib_data, ib_new_size );
        this->layers[ renderOrder ].ib_reflect.set_data( ib_data_reflect, ib_new_size );

        free( chunk_ib_data[ renderOrder ] );
        free( chunk_ib_data_reflect[ renderOrder ] );
    }
}

int Chunk::get_coords_from_index( const int index, int &out_x, int &out_y, int &out_z ) {
    const int y = ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1;
    const int x = ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1;
    const int z = ( index % ( CHUNK_SIZE_INTERNAL_Z ) ) - 1;
    // return 1;
    const int result = x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE_X && y < CHUNK_SIZE_Y && z < CHUNK_SIZE_Z;
    out_x = x;
    out_y = y;
    out_z = z;
    return result;
}

void Chunk::init( const VertexBuffer &vb_block_solid, const VertexBuffer &vb_block_water, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords ) {
    if constexpr ( REMEMBER_BLOCKS ) {
        this->blocks = static_cast<BlockState *>( calloc( CHUNK_BLOCK_SIZE, sizeof( BlockState ) ) );
    }

    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        const VertexBuffer *vb_prt;
        switch ( renderOrder ) {
            case RenderOrder_Water:
                vb_prt = &vb_block_water;
                break;
            default:
                vb_prt = &vb_block_solid;
        }
        const VertexBuffer &vb = *vb_prt;

        RenderLayer &renderLayer = this->layers[ renderOrder ];
        renderLayer.ib.init( );
        renderLayer.ib_reflect.init( );
        renderLayer.vb_coords.init( );
        renderLayer.va.init( );
        renderLayer.va.add_buffer( vb, vbl_block );
        renderLayer.va.add_buffer( renderLayer.vb_coords, vbl_coords );
    }
}

void Chunk::destroy( ) {
    if constexpr ( REMEMBER_BLOCKS ) {
        free( this->blocks );
    }

    for ( RenderLayer &layer : this->layers ) {
        if ( layer.populated_blocks ) {
            free( layer.populated_blocks );
        }
        RenderLayer &renderLayer = layer;
        renderLayer.populated_blocks = nullptr;
        renderLayer.ib.destroy( );
        renderLayer.ib_reflect.destroy( );
        renderLayer.vb_coords.destroy( );
        renderLayer.va.destroy( );
    }
}

void Chunk::draw( const Renderer &renderer, const Texture &texture, const Shader &shader, RenderOrder renderOrder, bool draw_reflect ) const {
    const RenderLayer &renderLayer = this->layers[ renderOrder ];
    if ( this->should_render && renderLayer.num_instances != 0 ) {
        if ( this->is_loading ) {
            pr_debug( "Error, attempting to render loading chunk" );
        }
        const IndexBuffer *active_ib_prt;
        if ( draw_reflect ) {
            active_ib_prt = &renderLayer.ib_reflect;
        } else {
            active_ib_prt = &renderLayer.ib;
        }
        const IndexBuffer &active_ib = *active_ib_prt;
        if ( renderOrder == RenderOrder_Flowers ) {
            glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        } else {
            glTexParameteri( texture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
            glTexParameteri( texture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );
        }
        renderer.draw( renderLayer.va, active_ib, shader, renderLayer.num_instances );
    }
}

BlockState Chunk::get_block( const glm::ivec3 &pos ) const {
    if constexpr ( !REMEMBER_BLOCKS ) {
        return BLOCK_STATE_AIR;
    }
    if ( this->blocks == nullptr ) {
        // pr_debug("Chunk has no blocks");
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    if ( pos.x > CHUNK_SIZE_X + 1 || //
         pos.y > CHUNK_SIZE_Y + 1 || //
         pos.z > CHUNK_SIZE_Z + 1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    if ( pos.x < -1 || //
         pos.y < -1 || //
         pos.z < -1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    return this->blocks[ get_index_from_coords( pos ) ];
}

void Chunk::set_block( const glm::ivec3 &pos, const BlockState &blockState ) const {
    if ( this->blocks == nullptr ) {
        // pr_debug("Chunk has no blocks");
        return;
    }
    if ( pos.x > CHUNK_SIZE_X + 1 || //
         pos.y > CHUNK_SIZE_Y + 1 || //
         pos.z > CHUNK_SIZE_Z + 1 ) {
        return;
    }
    if ( pos.x < -1 || //
         pos.y < -1 || //
         pos.z < -1 ) {
        return;
    }

    // Update the block in the client...
    this->blocks[ get_index_from_coords( pos ) ] = blockState;
}

void Chunk::set_block_by_index_if_different( int index, const BlockState *blockState ) {
    BlockState &oldBlockState = this->blocks[ index ];
    if ( BlockStates_equal( oldBlockState, *blockState ) ) {
        return;
    }
    this->blocks[ index ] = *blockState;
    this->dirty = true;
    this->needs_repopulation = true;
}

void Chunk::persist( MapStorage &map_storage ) const {
    if constexpr ( REMEMBER_BLOCKS ) {
        map_storage.persist_chunk( *this );
    }
}

int firstTime = 1;

void Chunk::load_terrain( MapStorage &map_storage ) {
    if constexpr ( !REMEMBER_BLOCKS ) {
        this->blocks = static_cast<BlockState *>( calloc( CHUNK_BLOCK_SIZE, sizeof( BlockState ) ) );
    }
    // pr_debug( "Loading chunk terrain x:%d y:%d z:%d", this->chunk_x, this->chunk_y, this->chunk_z );
    const int loaded = map_storage.load_chunk( *this );
    if ( !loaded ) {
        // We haven't loaded this chunk before, map gen it.
        if ( LOAD_CHUNKS_SUPPORTS_CUDA && MapGen::supports_cuda( ) ) {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using CUDA" );
            }
            MapGen::load_block_cuda( this );
        } else {
            if ( firstTime ) {
                firstTime = 0;
                pr_debug( "Using C" );
            }
            MapGen::load_block_c( this );
        }
        StructureGen::place( *this );
        this->dirty = 0;
    }
    this->calculate_populated_blocks( );
}

int Chunk::can_extend_rect( const BlockState &blockState, const unsigned int *packed_lighting, const WorkingSpace *workingSpace, const glm::ivec3 &starting, const glm::ivec3 &size, const glm::ivec3 &dir ) const {
    if constexpr ( DISABLE_GROUPING_BLOCKS ) {
        return 0;
    }
    if ( starting.x + size.x + dir.x > CHUNK_SIZE_X )
        return 0;
    if ( starting.y + size.y + dir.y > CHUNK_SIZE_Y )
        return 0;
    if ( starting.z + size.z + dir.z > CHUNK_SIZE_Z )
        return 0;
    // if ( ( ( size_x > 1 ) + ( size_y > 1 ) + ( size_z > 1 ) ) == 3 ) {
    //     if ( !( size_x == 1 && size_y == 1 && size_z == 1 ) ) {
    //         pr_debug( "Error, there must be 2 values different in a plane" );
    //     }
    // }
    // if ( blockState.id == REDSTONE_CROSS ) {
    //     pr_debug( "Trying to mesh" );
    // }
    int num_checked_blocks = 0;
    for ( int new_x = starting.x; new_x < starting.x + size.x; new_x++ ) {
        for ( int new_y = starting.y; new_y < starting.y + size.y; new_y++ ) {
            for ( int new_z = starting.z; new_z < starting.z + size.z; new_z++ ) {
                const int index = Chunk::get_index_from_coords( new_x + dir.x, new_y + dir.y, new_z + dir.z );
                num_checked_blocks++;
                if ( workingSpace[ index ].has_been_drawn ) {
                    return 0;
                }
                if ( !workingSpace[ index ].can_be_seen ) {
                    return 0;
                }
                BlockState new_blockState = this->blocks[ index ];
                if ( !BlockStates_equal( new_blockState, blockState ) ) {
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
    int expected_checked_blocks = size.x * size.y * size.z;
    if ( num_checked_blocks != expected_checked_blocks ) {
        pr_debug( "Error, Didnt check enough blocks" );
    }
    return 1;
}

inline int min( const int a, const int b, const int c, const int d ) {
    const int diag1 = ( a ) + ( d );
    const int diag2 = ( b ) + ( c );
    const int result = ( diag1 < diag2 ? diag1 : diag2 );
    return result;
}

inline int get_rotated_face( const int face, const int rotation ) {
    int result = face;
    for ( int i = 0; i < rotation; i++ ) {
        result = FACE_ROTATE_90[ result ];
    }
    return result;
}

void Chunk::calculate_populated_blocks( ) {
    int num_instances[ LAST_RENDER_ORDER ] = { 0 };

    for ( RenderLayer &layer : this->layers ) {
        if ( layer.populated_blocks ) {
            pr_debug( "Error, populated blocks already populated" );
        }
        layer.populated_blocks = static_cast<BlockCoords *>( calloc( CHUNK_BLOCK_SIZE, sizeof( BlockCoords ) ) );
    }
    WorkingSpace *workingSpace = static_cast<WorkingSpace *>( calloc( CHUNK_BLOCK_SIZE, sizeof( WorkingSpace ) ) );

    for ( int index = CHUNK_BLOCK_DRAW_START; index < CHUNK_BLOCK_DRAW_STOP; index++ ) {
        int x, y, z;
        if ( Chunk::get_coords_from_index( index, x, y, z ) ) {
            BlockState blockState = this->blocks[ index ];
            BlockID blockID = blockState.id;
            Block *block = block_definition_get_definition( blockID );

            workingSpace[ index ].visible = render_order_is_visible( block->renderOrder );

            if ( workingSpace[ index ].visible ) {
                int block_is_visiable = 0;
                int visible_from[ NUM_FACES_IN_CUBE ] = { 0, 0, 0, 0, 0, 0 };
                for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
                    int rotated_face = get_rotated_face( face, blockState.rotation );
                    BlockState block_next_to_state = this->blocks[ get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face ], y + FACE_DIR_Y_OFFSETS[ face ], z + FACE_DIR_Z_OFFSETS[ face ] ) ];

                    Block *block_next_to = block_definition_get_definition( block_next_to_state.id );

                    int opposing_face = get_rotated_face( face, block_next_to_state.rotation );
                    opposing_face = OPPOSITE_FACE[ opposing_face ];

                    bool is_seethrough = block->calculated.is_seethrough_face[ rotated_face ];
                    if ( is_seethrough && block->calculated.hides_self[ rotated_face ] ) { // water and glass
                        if ( face == FACE_TOP && block->renderOrder == RenderOrder_Water ) {
                            visible_from[ face ] = block_next_to->id != block->id;
                        } else {
                            visible_from[ face ] = block_next_to->calculated.is_seethrough_face[ opposing_face ] && block_next_to_state.id != blockID;
                        }
                    } else if ( is_seethrough ) { // The current block is seethough on this face. Check all the directions not opposing it;
                        bool visable_through_opposing = false;
                        for ( int face_around = FACE_TOP; face_around < NUM_FACES_IN_CUBE; face_around++ ) {
                            if ( face_around == opposing_face ) {
                                // It's the non_opposing_face and parallel with this face, don't count it
                                continue;
                            }
                            BlockState block_around_state = this->blocks[ get_index_from_coords( x + FACE_DIR_X_OFFSETS[ face_around ], y + FACE_DIR_Y_OFFSETS[ face_around ], z + FACE_DIR_Z_OFFSETS[ face_around ] ) ];
                            Block *block_around = block_definition_get_definition( block_around_state.id );
                            int around_face = get_rotated_face( face_around, block_around_state.rotation );
                            around_face = OPPOSITE_FACE[ around_face ];
                            if ( block_around->calculated.is_seethrough_face[ around_face ] ) {
                                visable_through_opposing = true;
                                break;
                            }
                        }
                        visible_from[ face ] = visable_through_opposing;

                    } else {
                        bool visable_through_opposing = block_next_to->calculated.is_seethrough_face[ opposing_face ];
                        visible_from[ face ] = visable_through_opposing;
                    }
                    block_is_visiable |= visible_from[ face ];
                }
                {
                    int can_be_shaded = render_order_can_be_shaded( block->renderOrder );

                    // 1 Offset
                    int xplus = x + 1;
                    int xminus = x - 1;
                    int yplus = y + 1;
                    int yminus = y - 1;
                    int zplus = z + 1;
                    int zminus = z - 1;

                    int t = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, yplus, z + 0 ) ].id )->casts_shadow;
                    int bo = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, yminus, z + 0 ) ].id )->casts_shadow;
                    int f = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, y + 0, zplus ) ].id )->casts_shadow;
                    int ba = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, y + 0, zminus ) ].id )->casts_shadow;
                    int r = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, y + 0, z + 0 ) ].id )->casts_shadow;
                    int l = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, y + 0, z + 0 ) ].id )->casts_shadow;

                    // If the block is visible and can be shaded, check neighbors for shade and populated the packed lighting
                    // 2 Offsets
                    int tl = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, yplus, z + 0 ) ].id )->casts_shadow;
                    int tr = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, yplus, z + 0 ) ].id )->casts_shadow;
                    int tf = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, yplus, zplus ) ].id )->casts_shadow;
                    int tba = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, yplus, zminus ) ].id )->casts_shadow;

                    int bol = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, yminus, z + 0 ) ].id )->casts_shadow;
                    int bor = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, yminus, z + 0 ) ].id )->casts_shadow;
                    int bof = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, yminus, zplus ) ].id )->casts_shadow;
                    int boba = block_definition_get_definition( this->blocks[ get_index_from_coords( x + 0, yminus, zminus ) ].id )->casts_shadow;

                    int fl = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, y + 0, zplus ) ].id )->casts_shadow;
                    int bal = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, y + 0, zminus ) ].id )->casts_shadow;
                    int fr = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, y + 0, zplus ) ].id )->casts_shadow;
                    int bar = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, y + 0, zminus ) ].id )->casts_shadow;

                    // 3 Offsetes
                    int tfl = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, yplus, zplus ) ].id )->casts_shadow;
                    int tbl = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, yplus, zminus ) ].id )->casts_shadow;
                    int tfr = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, yplus, zplus ) ].id )->casts_shadow;
                    int tbr = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, yplus, zminus ) ].id )->casts_shadow;

                    int bfl = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, yminus, zplus ) ].id )->casts_shadow;
                    int bbl = block_definition_get_definition( this->blocks[ get_index_from_coords( xminus, yminus, zminus ) ].id )->casts_shadow;
                    int bfr = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, yminus, zplus ) ].id )->casts_shadow;
                    int bbr = block_definition_get_definition( this->blocks[ get_index_from_coords( xplus, yminus, zminus ) ].id )->casts_shadow;
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
                }
                workingSpace[ index ].can_be_seen = block_is_visiable;
            } else {
                workingSpace[ index ].can_be_seen = false;
            }
        }
    }
    for ( int y = 0; y < CHUNK_SIZE_Y; y++ ) {
        for ( int x = 0; x < CHUNK_SIZE_X; x++ ) {
            for ( int z = 0; z < CHUNK_SIZE_Z; z++ ) {
                int index = get_index_from_coords( x, y, z );
                if ( !workingSpace[ index ].has_been_drawn ) {

                    BlockState blockState = this->blocks[ index ];
                    Block *block = block_definition_get_definition( blockState.display_id );
                    // if ( blockState.display_id == REDSTONE_CROSS ) {
                    //     pr_debug( "Got cross" );
                    // }
                    bool visiable_block = workingSpace[ index ].visible;
                    bool can_be_seen = workingSpace[ index ].can_be_seen;
                    bool has_been_drawn = workingSpace[ index ].has_been_drawn;
                    unsigned int *packed_lighting = workingSpace[ index ].packed_lighting;

                    if ( visiable_block && can_be_seen && !has_been_drawn ) {

                        int block_can_mesh_x;
                        int block_can_mesh_y;
                        int block_can_mesh_z;

                        if ( blockState.rotation == BLOCK_ROTATE_0 || blockState.rotation == BLOCK_ROTATE_180 ) {
                            block_can_mesh_x = block->calculated.can_mesh_x;
                            block_can_mesh_y = block->calculated.can_mesh_y;
                            block_can_mesh_z = block->calculated.can_mesh_z;
                        } else {
                            block_can_mesh_x = block->calculated.can_mesh_z;
                            block_can_mesh_y = block->calculated.can_mesh_y;
                            block_can_mesh_z = block->calculated.can_mesh_x;
                        }

                        int size_x = 1;
                        int size_y = 1;
                        int size_z = 1;
                        int can_extend_x = 0;
                        int can_extend_y = 0;
                        int can_extend_z = 0;
                        do {
                            if ( block_can_mesh_x ) {
                                glm::ivec3 starting = glm::ivec3( x + size_x - 1, y, z );
                                glm::ivec3 size = glm::ivec3( 1, size_y, size_z );
                                glm::ivec3 dir = glm::ivec3( 1, 0, 0 );
                                can_extend_x = this->can_extend_rect( blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_x ) {
                                    size_x++;
                                }
                            }
                            if ( block_can_mesh_z ) {
                                glm::ivec3 starting = glm::ivec3( x, y, z + size_z - 1 );
                                glm::ivec3 size = glm::ivec3( size_x, size_y, 1 );
                                glm::ivec3 dir = glm::ivec3( 0, 0, 1 );
                                can_extend_z = this->can_extend_rect( blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_z ) {
                                    size_z++;
                                }
                            }
                            if ( block_can_mesh_y ) {
                                glm::ivec3 starting = glm::ivec3( x, y + size_y - 1, z );
                                glm::ivec3 size = glm::ivec3( size_x, 1, size_z );
                                glm::ivec3 dir = glm::ivec3( 0, 1, 0 );
                                can_extend_y = this->can_extend_rect( blockState, packed_lighting, workingSpace, starting, size, dir );
                                if ( can_extend_y ) {
                                    size_y++;
                                }
                            }
                        } while ( can_extend_x || can_extend_z || can_extend_y );

                        for ( int new_x = x; new_x < x + size_x; new_x++ ) {
                            for ( int new_z = z; new_z < z + size_z; new_z++ ) {
                                for ( int new_y = y; new_y < y + size_y; new_y++ ) {
                                    int index2 = get_index_from_coords( new_x, new_y, new_z );
                                    workingSpace[ index2 ].has_been_drawn = true;
                                }
                            }
                        }
                        workingSpace[ index ].has_been_drawn = true;

                        BlockCoords *blockCoord;

                        blockCoord = &this->layers[ block->renderOrder ].populated_blocks[ num_instances[ block->renderOrder ] ];
                        num_instances[ block->renderOrder ] += 1;

                        blockCoord->x = this->chunk_pos.x * CHUNK_SIZE_X + x;
                        blockCoord->y = this->chunk_pos.y * CHUNK_SIZE_Y + y;
                        blockCoord->z = this->chunk_pos.z * CHUNK_SIZE_Z + z;

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

    if constexpr ( !REMEMBER_BLOCKS ) {
        free( this->blocks );
    }
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        this->layers[ renderOrder ].num_instances = num_instances[ renderOrder ];
    }
}

void Chunk::program_terrain( ) {
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &renderLayer = this->layers[ renderOrder ];
        if ( renderLayer.num_instances != 0 ) {
            renderLayer.vb_coords.set_data( renderLayer.populated_blocks, sizeof( BlockCoords ) * renderLayer.num_instances );
            this->should_render = 1;
        }
        free( renderLayer.populated_blocks );
        renderLayer.populated_blocks = nullptr;
    }
}

void Chunk::unprogram_terrain( ) {
    this->should_render = 0;
    for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
        RenderLayer &renderLayer = this->layers[ renderOrder ];
        if ( renderLayer.populated_blocks ) {
            free( renderLayer.populated_blocks );
        }
        renderLayer.populated_blocks = nullptr;
    }
}