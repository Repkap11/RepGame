#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"
#include "common/abstract/shader.hpp"

#define ISOMETRIC_FACES 3

float verticies[][ 2 ] = {
    // Top
    { 0, 0 },     // 0
    { -1, 0.5f }, // 1
    { 1, 0.5f },  // 2
    { 0, 1 },     // 3
    // Front
    { -1, -0.5f }, // 4
    { -1, 0.5f },
    { 0, -1 }, // 5
    { 0, 0 },
    // Right
    { 0, -1 },
    { 0, 0 },
    { 1, -0.5f }, // 6
    { 1, 0.5f },
};

int inventory_isometric_face[] = { FACE_TOP, FACE_FRONT, FACE_RIGHT };
float tints_for_light[] = { 1, 0.8, 0.6 };
int num_points_per_block = 4;
int num_index_per_block = 18;
int num_blocks_max = INVENTORY_MAX_SIZE;
int vb_max_size = num_blocks_max * num_points_per_block * ISOMETRIC_FACES;
int ib_max_size = num_blocks_max * num_index_per_block * ISOMETRIC_FACES;

void InventoryRenderer::init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance ) {
    this->render_chain_inventory.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, NULL, 0, NULL, 0 );
    this->render_chain_inventory.create_instance( );

    this->ib_data_inventory = ( unsigned int * )calloc( ib_max_size, sizeof( unsigned int ) );
    this->vb_data_inventory = ( UIOverlayVertex * )calloc( vb_max_size, sizeof( UIOverlayVertex ) );

    index_buffer_init( &this->ib );
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
    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        InventorySlot *inventory_slot = &inventory_slots[ i_slot ];
        BlockID block_id = inventory_slot->block_id;
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

        unsigned int *ui_ib = &this->ib_data_inventory[ i_data * num_index_per_block ];
        // Top
        ui_ib[ 0 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 0;
        ui_ib[ 1 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 3;
        ui_ib[ 2 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 1;
        ui_ib[ 3 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 3;
        ui_ib[ 4 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 0;
        ui_ib[ 5 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 2;
        // Front
        ui_ib[ 6 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 4;
        ui_ib[ 7 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 7;
        ui_ib[ 8 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 5;
        ui_ib[ 9 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 7;
        ui_ib[ 10 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 4;
        ui_ib[ 11 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 6;
        // Right
        ui_ib[ 12 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 8;
        ui_ib[ 13 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 11;
        ui_ib[ 14 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 9;
        ui_ib[ 15 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 11;
        ui_ib[ 16 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 8;
        ui_ib[ 17 ] = i_data * num_points_per_block * ISOMETRIC_FACES + 10;

        for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
            for ( int i_point = 0; i_point < num_points_per_block; i_point++ ) {
                // Get the block definition to see if it should be displayed isometrically or not.
                int vb_index = i_data * num_points_per_block * ISOMETRIC_FACES + i_point + num_points_per_block * face;
                UIOverlayVertex *ui_vertex = &this->vb_data_inventory[ vb_index ];

                ui_vertex->texture.x = ( i_point / 2 );
                ui_vertex->texture.y = ( i_point % 2 );
                ui_vertex->is_block = 1;
                float tint_for_light = tints_for_light[ face ];
                if ( !block->casts_shadow ) {
                    tint_for_light = 1.0f;
                }
                ui_vertex->tint = { tint_for_light, tint_for_light, tint_for_light, 1 };

                if ( !block->icon_is_isometric ) {
                    // Like Reeds
                    ui_vertex->screen_x = block_corner_x + icon_size * ( ui_vertex->texture.x * 0.98 );
                    ui_vertex->screen_y = block_corner_y + icon_size * ( ui_vertex->texture.y * 0.98 );

                    ui_vertex->texture.id = block->inventory_non_isometric_id - 1;
                } else {
                    // Like grass
                    ui_vertex->screen_x = block_corner_x + icon_size * ( verticies[ i_point + num_points_per_block * face ][ 0 ] / 2 + 0.5 );
                    ui_vertex->screen_y = block_corner_y + icon_size * ( verticies[ i_point + num_points_per_block * face ][ 1 ] / 2 + 0.5 );

                    // ui_vertex->texture.id = ( block->textures[ inventory_isometric_face[ face ] ] - 1 );
                    ui_vertex->texture.id = ( block->textures[ inventory_isometric_face[ face ] ] - 1 );
                }
            }
        }
    }

    int vb_size = ( num_blocks_max - skipped_blocks ) * num_points_per_block * ISOMETRIC_FACES;
    int ib_size = ( num_blocks_max - skipped_blocks ) * num_index_per_block * ISOMETRIC_FACES;

    index_buffer_set_data( &this->ib, this->ib_data_inventory, ib_size );
    this->render_chain_inventory.update_element( this->vb_data_inventory, vb_size );
}

void InventoryRenderer::draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader ) {
    showErrors( );
    this->render_chain_inventory.draw( renderer, shader, &this->ib );
    showErrors( );
}

void InventoryRenderer::cleanup( ) {

    free( this->vb_data_inventory );
    free( this->ib_data_inventory );
}