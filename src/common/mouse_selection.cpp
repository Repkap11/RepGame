#include "common/RepGame.hpp"
#include "common/mouse_selection.hpp"
#include "common/renderer/renderer.hpp"
#include "common/block.hpp"

#define SELECTION_SIZE_OFFSET ( 0.0003f )

static CubeFace vd_data_selection[] = {
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
    auto pair = this->render_chain_mouse_selection.create_instance( );
    this->entity = pair.first;
    BlockCoords &blockCoords = pair.second;

    for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
        blockCoords.face[ i ] = PLAYER_SELECTION - 1;
    }
    blockCoords.x = 0;
    blockCoords.y = 0;
    blockCoords.z = 0;
    blockCoords.mesh_x = 1;
    blockCoords.mesh_y = 1;
    blockCoords.mesh_z = 1;
    this->shouldDraw = 0;

    blockCoords.scale_x = 1;
    blockCoords.scale_y = 1;
    blockCoords.scale_z = 1;

    blockCoords.offset_x = 0;
    blockCoords.offset_y = 0;
    blockCoords.offset_z = 0;

    blockCoords.tex_offset_x = 0;
    blockCoords.tex_offset_y = 0;
    blockCoords.tex_offset_z = 0;
}
void MouseSelection::set_block( const glm::ivec3 &pos, int shouldDraw, BlockState blockState ) {
    BlockCoords &blockCoords = this->render_chain_mouse_selection.get_instance( this->entity );
    // y += 1;
    this->shouldDraw = shouldDraw;
    if ( blockCoords.x != pos.x ||                                    //
         blockCoords.y != pos.y ||                                    //
         blockCoords.z != pos.z ||                                    //
         !BlockStates_equal( blockState, this->selected_block_state ) //
    ) {
        Block *block = block_definition_get_definition( blockState.id );
        blockCoords.x = pos.x;
        blockCoords.y = pos.y;
        blockCoords.z = pos.z;
        this->selected_block_state = blockState;

        blockCoords.scale_x = PIXEL_TO_FLOAT( block->scale.x );
        blockCoords.scale_y = PIXEL_TO_FLOAT( block->scale.y );
        blockCoords.scale_z = PIXEL_TO_FLOAT( block->scale.z );

        blockCoords.offset_x = PIXEL_TO_FLOAT( block->offset.x );
        blockCoords.offset_y = PIXEL_TO_FLOAT( block->offset.y );
        blockCoords.offset_z = PIXEL_TO_FLOAT( block->offset.z );

        blockCoords.face_shift = blockState.rotation;

        this->render_chain_mouse_selection.invalidate( this->entity );
    }
}
void MouseSelection::draw( const Renderer &renderer, const Shader &shader ) {
    if ( this->shouldDraw ) {
        this->render_chain_mouse_selection.draw_lines( renderer, shader );
    }
}