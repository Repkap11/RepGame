#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"
#include "common/abstract/shader.hpp"

#define ISOMETRIC_FACES 3

// UIOverlayVertex vb_isometric[ 12 ] = {
//     { 0, 0, { 1, 0 }, ISO_FACE_TOP },     // 0
//     { -1, 0.5f, { 1, 1 }, ISO_FACE_TOP }, // 1
//     { 1, 0.5f, { 0, 0 }, ISO_FACE_TOP },  // 2
//     { 0, 1, { 0, 1 }, ISO_FACE_TOP },     // 3

//     { -1, -0.5f, { 1, 0 }, ISO_FACE_FRONT }, // 4
//     { -1, 0.5f, { 1, 1 }, ISO_FACE_FRONT },  // a
//     { 0, -1, { 0, 0 }, ISO_FACE_FRONT },     // 5
//     { 0, 0, { 0, 1 }, ISO_FACE_FRONT },      // b

//     { 0, -1, { 1, 0 }, ISO_FACE_RIGHT },    // c
//     { 0, 0, { 1, 1 }, ISO_FACE_RIGHT },     // d
//     { 1, -0.5f, { 0, 0 }, ISO_FACE_RIGHT }, // 6
//     { 1, 0.5f, { 0, 1 }, ISO_FACE_RIGHT },  // e
// };

// unsigned int ib_isometric[ 18 ] = {
//     // Top
//     // 0, 3, 1, 3, 0, 2, 4, 7, 5, 7, 4, 6, 8, 11, 9, 11, 8, 10,
//     0,  3,  1, //
//     3,  0,  2, //

//     4,  7,  5, //
//     7,  4,  6, //

//     8,  11, 9,  //
//     11, 8,  10, //
// };

// UIOverlayVertex vb_quad[] = {

// };

// unsigned int ib_quad[ 3 * 2 ] = {
//     1, 0, 2, //
//     1, 2, 3  //
// };

#define VB_ISOMETRIC_QUAD_SIZE 16
static const UIOverlayVertex vb_isometric_quad[ VB_ISOMETRIC_QUAD_SIZE ] = {
    // Isometric
    { 0, 0, { 1, 0 }, 1, ISO_FACE_TOP },     // 0
    { -1, 0.5f, { 1, 1 }, 1, ISO_FACE_TOP }, // 1
    { 1, 0.5f, { 0, 0 }, 1, ISO_FACE_TOP },  // 2
    { 0, 1, { 0, 1 }, 1, ISO_FACE_TOP },     // 3

    { -1, -0.5f, { 1, 0 }, 1, ISO_FACE_FRONT }, // 4
    { -1, 0.5f, { 1, 1 }, 1, ISO_FACE_FRONT },  // a
    { 0, -1, { 0, 0 }, 1, ISO_FACE_FRONT },     // 5
    { 0, 0, { 0, 1 }, 1, ISO_FACE_FRONT },      // b

    { 0, -1, { 1, 0 }, 1, ISO_FACE_RIGHT },    // c
    { 0, 0, { 1, 1 }, 1, ISO_FACE_RIGHT },     // d
    { 1, -0.5f, { 0, 0 }, 1, ISO_FACE_RIGHT }, // 6
    { 1, 0.5f, { 0, 1 }, 1, ISO_FACE_RIGHT },  // e

    // Quad
    { 0, 0, { 0, 0 }, 0, ISO_FACE_FRONT }, //
    { 0, 1, { 0, 1 }, 0, ISO_FACE_FRONT }, //
    { 1, 0, { 1, 0 }, 0, ISO_FACE_FRONT }, //
    { 1, 1, { 1, 1 }, 0, ISO_FACE_FRONT }  //
};

#define IB_ISOMETRIC_QUAD_SIZE 24
static const unsigned int ib_isometric_quad[ IB_ISOMETRIC_QUAD_SIZE ] = {
    // Top
    // 0, 3, 1, 3, 0, 2, 4, 7, 5, 7, 4, 6, 8, 11, 9, 11, 8, 10,
    0,  3,  1, //
    3,  0,  2, //

    4,  7,  5, //
    7,  4,  6, //

    8,  11, 9,  //
    11, 8,  10, //

    13, 12, 14, //
    13, 14, 15  //
};

static int inventory_isometric_face[] = { FACE_TOP, FACE_FRONT, FACE_RIGHT };
float tints_for_light[] = { 1, 0.8, 0.6 };
int num_points_per_block = 4;
int num_index_per_block = 18;
int num_blocks_max = INVENTORY_MAX_SIZE;

int vb_max_size = num_blocks_max * num_points_per_block * ISOMETRIC_FACES;
int ib_max_size = num_blocks_max * num_index_per_block * ISOMETRIC_FACES;

void InventoryRenderer::init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance ) {
    this->render_chain_inventory_icons.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad, 16, ib_isometric_quad, 24 );

    // this->render_chain_inventory_background.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, NULL, 0, ib_quad, 6 );
}

void InventoryRenderer::onSizeChange( int width, int height ) {
    this->inv_height = height * 0.8;
    this->inv_width = width * 0.8;

    this->inv_x = -width / 2 + ( width - this->inv_width ) / 2;
    this->inv_y = -height / 2 + ( height - this->inv_height ) / 2;
}

void InventoryRenderer::render( InventorySlot *inventory_slots ) {
    int item_size = this->inv_width / INVENTORY_BLOCKS_PER_ROW;
    int icon_size = item_size * 0.8;
    int icon_offset = ( icon_size - item_size ) / 2;

    int skipped_blocks = 0;
    render_chain_inventory_icons.cleanup( );
    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        InventorySlot *inventory_slot = &inventory_slots[ i_slot ];
        BlockID block_id = inventory_slot->block_id;
        if ( block_id == LAST_BLOCK_ID ) {
            skipped_blocks++;
            continue;
        }
        Block *block = block_definition_get_definition( block_id );
        if ( block->renderOrder == RenderOrder_Transparent ) {
            skipped_blocks++;
            continue;
        }

        int block_grid_coord_x = i_slot % INVENTORY_BLOCKS_PER_ROW;
        int block_grid_coord_y = i_slot / INVENTORY_BLOCKS_PER_ROW;
        int block_corner_x = this->inv_x + block_grid_coord_x * item_size;
        int block_corner_y = this->inv_y + block_grid_coord_y * item_size;
        int i_data = i_slot - skipped_blocks;

        // for ( int i_point = 0; i_point < num_points_per_block; i_point++ ) {
        auto pair = this->render_chain_inventory_icons.create_instance( );
        UIOverlayInstance &ui_vertex = pair.second;

        int vb_index = i_data;
        float cell_x = ( i_slot / 2 );
        float cell_y = ( i_slot % 2 );
        ui_vertex.is_block = 1;

        ui_vertex.is_isometric = block->icon_is_isometric;
        // Like Reeds
        ui_vertex.screen_x = block_corner_x;
        ui_vertex.screen_y = block_corner_y;
        ui_vertex.width = icon_size * 0.98;
        ui_vertex.height = icon_size * 0.98;
        // } else {
        // Like grass
        // ui_vertex.screen_x = block_corner_x + icon_size * ( verticies_isometric[ i_point + num_points_per_block * face ][ 0 ] / 2 + 0.5 );
        // ui_vertex.screen_y = block_corner_y + icon_size * ( verticies_isometric[ i_point + num_points_per_block * face ][ 1 ] / 2 + 0.5 );
        // }

        for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
            // Get the block definition to see if it should be displayed isometrically or not.

            float tint_for_light = tints_for_light[ face ];
            if ( !block->casts_shadow ) {
                tint_for_light = 1.0f;
            }
            ui_vertex.tint[ face ] = tint_for_light;

            if ( !block->icon_is_isometric ) {
                // Like Reeds
                ui_vertex.id_isos[ face ] = block->inventory_non_isometric_id - 1;
            } else {
                // Like grass
                ui_vertex.id_isos[ face ] = ( block->textures[ inventory_isometric_face[ face ] ] - 1 );
            }
            // }
        }
        ui_vertex.tint[ 3 ] = 1;
    }
}

void InventoryRenderer::draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader ) {
    showErrors( );
    this->render_chain_inventory_icons.draw( renderer, shader );
    // this->render_chain_inventory_background.draw( renderer, shader );
    showErrors( );
}

void InventoryRenderer::cleanup( ) {
    // this->render_chain_inventory_background.cleanup( );
    this->render_chain_inventory_icons.cleanup( );
}