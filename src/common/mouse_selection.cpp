#include "common/mouse_selection.hpp"
#include "common/block.hpp"
#include "common/world.hpp"

MK_SHADER( selection_vertex );
MK_SHADER( selection_fragment );

#define SELECTION_SIZE_OFFSET ( 0.0003f )

constexpr static CubeFace vd_data_selection[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    { 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_BACK, CORNER_OFFSET_bbl }, // 0
    { 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_BACK, CORNER_OFFSET_bbr }, // 1
    { 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_BACK, CORNER_OFFSET_tbr }, // 2
    { 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_BACK, CORNER_OFFSET_tbl }, // 3

    { 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_FRONT, CORNER_OFFSET_bfl }, // 4
    { 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_FRONT, CORNER_OFFSET_bfr }, // 5
    { 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_FRONT, CORNER_OFFSET_tfr }, // 6
    { 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_FRONT, CORNER_OFFSET_tfl }, // 7

    { 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_LEFT, CORNER_OFFSET_bbl },  // 8
    { 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_RIGHT, CORNER_OFFSET_bbr }, // 9
    { 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_RIGHT, CORNER_OFFSET_tbr }, // 10
    { 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_LEFT, CORNER_OFFSET_tbl },  // 11

    { 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_LEFT, CORNER_OFFSET_bfl },  // 12
    { 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_RIGHT, CORNER_OFFSET_bfr }, // 13
    { 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_RIGHT, CORNER_OFFSET_tfr }, // 14
    { 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_LEFT, CORNER_OFFSET_tfl },  // 15

    { 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl }, // 16
    { 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr }, // 17
    { 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_TOP, CORNER_OFFSET_tbr },    // 18
    { 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_TOP, CORNER_OFFSET_tbl },    // 19

    { 0.0f - SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl }, // 20
    { 1.0f + SELECTION_SIZE_OFFSET, 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr }, // 21
    { 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0, 1, FACE_TOP, CORNER_OFFSET_tfr },    // 22
    { 0.0f - SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 1, 1, FACE_TOP, CORNER_OFFSET_tfl },    // 23

    { 0.5f, 1.0f + SELECTION_SIZE_OFFSET, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_TOP, CORNER_OFFSET_c },    // 24
    { 0.5f, 0.0f - SELECTION_SIZE_OFFSET, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BOTTOM, CORNER_OFFSET_c }, // 25
    { 1.0f + SELECTION_SIZE_OFFSET, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_RIGHT, CORNER_OFFSET_c },  // 26
    { 0.5f, 0.5f, 1.0f + SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_FRONT, CORNER_OFFSET_c },  // 27
    { 0.0f - SELECTION_SIZE_OFFSET, 0.5f, 0.5f, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_LEFT, CORNER_OFFSET_c },   // 28
    { 0.5f, 0.5f, 0.0f - SELECTION_SIZE_OFFSET, /*Coords  Texture coords*/ 0.5f, 0.5f, FACE_BACK, CORNER_OFFSET_c },   // 29
};
#define VB_DATA_SIZE_SELECTION ( 4 * 6 + 6 )

void MouseSelection::init( const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords ) {
    this->render_chain_mouse_selection.init( vbl_block, vbl_coords, vd_data_selection, VB_DATA_SIZE_SELECTION, ib_data_solid, IB_SOLID_SIZE );
    for ( int i = 0; i < STAIRS_MAX_BOXES; i++ ) {
        const auto pair = this->render_chain_mouse_selection.create_instance( );
        this->entity[ i ] = pair.first;
        BlockCoords &blockCoords = pair.second;

        for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
            blockCoords.face[ f ] = TEX_PLAYER_SELECTION;
        }
        blockCoords.x = 0;
        blockCoords.y = 0;
        blockCoords.z = 0;
        blockCoords.mesh_x = 1;
        blockCoords.mesh_y = 1;
        blockCoords.mesh_z = 1;

        // Instance 0 is the single-box outline; the rest hide at zero scale
        // until a multi-box model claims them.
        blockCoords.scale_x = i == 0 ? 16 : 0;
        blockCoords.scale_y = i == 0 ? 16 : 0;
        blockCoords.scale_z = i == 0 ? 16 : 0;

        blockCoords.offset_x = 0;
        blockCoords.offset_y = 0;
        blockCoords.offset_z = 0;

        blockCoords.tex_offset_x = 0;
        blockCoords.tex_offset_y = 0;
        blockCoords.tex_offset_z = 0;
    }
    this->shouldDraw = 0;

    this->break_progress = 0.0f;
    this->shader.init( &selection_vertex, &selection_fragment );
}
void MouseSelection::set_break_progress( const float progress ) {
    this->break_progress = progress;
}
void MouseSelection::hide( ) {
    this->shouldDraw = 0;
}
void MouseSelection::set_block( const World &world, const glm::ivec3 &pos, const bool shouldDraw, const BlockState &blockState ) {
    Block *block = block_definition_get_definition( blockState.id );
    this->shouldDraw = shouldDraw;
    if ( block->is_stairs ) {
        // Outline the real stair boxes (slab + step + corner octet), already
        // yawed to world space by stairs_collision_boxes — the same gather
        // collision.cpp does, so a neighbor change re-shapes the outline too.
        static const glm::ivec3 stair_dirs[ 4 ] = {
            glm::ivec3( 0, 0, -1 ), glm::ivec3( 1, 0, 0 ), glm::ivec3( 0, 0, 1 ), glm::ivec3( -1, 0, 0 ),
        };
        int stair_rots[ 4 ];
        for ( int d = 0; d < 4; d++ ) {
            const BlockState neighbor = world.get_loaded_block( pos + stair_dirs[ d ] );
            stair_rots[ d ] = ( neighbor.id < LAST_BLOCK_ID && block_definition_get_definition( neighbor.id )->is_stairs ) ? neighbor.rotation : -1;
        }
        BlockBox boxes[ STAIRS_MAX_BOXES ];
        const int num_boxes = stairs_collision_boxes( blockState.rotation, stair_rots, boxes );
        for ( int i = 0; i < STAIRS_MAX_BOXES; i++ ) {
            BlockCoords &bc = this->render_chain_mouse_selection.get_instance( this->entity[ i ] );
            bc.x = pos.x;
            bc.y = pos.y;
            bc.z = pos.z;
            bc.face_shift = 0;
            if ( i < num_boxes ) {
                bc.scale_x = boxes[ i ].scale[ 0 ];
                bc.scale_y = boxes[ i ].scale[ 1 ];
                bc.scale_z = boxes[ i ].scale[ 2 ];
                bc.offset_x = boxes[ i ].offset[ 0 ];
                bc.offset_y = boxes[ i ].offset[ 1 ];
                bc.offset_z = boxes[ i ].offset[ 2 ];
            } else {
                bc.scale_x = 0;
                bc.scale_y = 0;
                bc.scale_z = 0;
            }
            this->render_chain_mouse_selection.invalidate( this->entity[ i ] );
        }
        this->selected_block_state = blockState;
        return;
    }
    // Non-stair: instance 0 is the whole outline, the rest collapse.
    for ( int i = 1; i < STAIRS_MAX_BOXES; i++ ) {
        BlockCoords &bc = this->render_chain_mouse_selection.get_instance( this->entity[ i ] );
        bc.scale_x = 0;
        bc.scale_y = 0;
        bc.scale_z = 0;
        this->render_chain_mouse_selection.invalidate( this->entity[ i ] );
    }
    BlockCoords &blockCoords = this->render_chain_mouse_selection.get_instance( this->entity[ 0 ] );
    if ( blockCoords.x != pos.x ||                                    //
         blockCoords.y != pos.y ||                                    //
         blockCoords.z != pos.z ||                                    //
         !BlockStates_equal( blockState, this->selected_block_state ) //
    ) {
        blockCoords.x = pos.x;
        blockCoords.y = pos.y;
        blockCoords.z = pos.z;
        this->selected_block_state = blockState;

        blockCoords.scale_x = block->scale.x;
        blockCoords.scale_y = block->scale.y;
        blockCoords.scale_z = block->scale.z;

        blockCoords.offset_x = block->offset.x;
        blockCoords.offset_y = block->offset.y;
        blockCoords.offset_z = block->offset.z;

        // Pressed buttons/plates render thinner, so the selection box matches.
        if ( block->is_pressure_plate && blockState.current_redstone_power > 0 ) {
            blockCoords.scale_y = 1;
        } else if ( block->is_button && blockState.current_redstone_power > 0 ) {
            blockCoords.scale_z = 1;
        }

        // Torches select on the wider 4px pick box (side mounts hug the wall).
        if ( block->is_torch ) {
            short torch_scale[ 3 ], torch_offset[ 3 ];
            torch_pick_shape( block, blockState.rotation, torch_scale, torch_offset );
            blockCoords.scale_x = torch_scale[ 0 ];
            blockCoords.scale_y = torch_scale[ 1 ];
            blockCoords.scale_z = torch_scale[ 2 ];
            blockCoords.offset_x = torch_offset[ 0 ];
            blockCoords.offset_y = torch_offset[ 1 ];
            blockCoords.offset_z = torch_offset[ 2 ];
        }

        // Top-half slab: the outline mirrors into the upper half of the cell.
        if ( block->is_slab && ( blockState.rotation & BLOCK_ROTATE_SLAB_TOP ) != 0 ) {
            blockCoords.offset_y = 16 - blockCoords.scale_y - blockCoords.offset_y;
        }

        blockCoords.face_shift = block_yaw( block, blockState.rotation );

        this->render_chain_mouse_selection.invalidate( this->entity[ 0 ] );
    }
}
void MouseSelection::draw( const Renderer &renderer, const glm::mat4 &mvp, const glm::vec3 &origin ) {
    if ( this->shouldDraw ) {
        this->shader.set_uniform_mat4f( "u_MVP", mvp );
        this->shader.set_uniform1f( "u_LineWidth", 3.0f );
        this->shader.set_uniform1f( "u_BreakProgress", this->break_progress );
        this->shader.set_uniform3f( "u_Origin", origin.x, origin.y, origin.z );
        this->render_chain_mouse_selection.draw( renderer, this->shader );
    }
}
void MouseSelection::destroy( ) {
    this->shader.destroy( );
}
