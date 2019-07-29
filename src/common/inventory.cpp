#include "common/RepGame.hpp"
#include "common/inventory.hpp"
#include "common/abstract/shader.hpp"

#define ISOMETRIC_FACES 3

float inventory_iso_rotations[][ 2 ] = {
    // Top
    { 0, 0 },
    { -1, 0.5f },
    { 1, 0.5f },
    { 0, 1 },
    // Front
    { -1, -0.5f },
    { -1, 0.5 },
    { 0, -1 },
    { 0, 0 },
    // Right
    { 0, -1 },
    { 0, 0 },
    { 1, -0.5f },
    { 1, 0.5f },
};

int inventory_isometric_face[] = { FACE_TOP, FACE_FRONT, FACE_RIGHT };

MK_SHADER( inventory_vertex );
MK_SHADER( inventory_fragment );

void inventory_init( Inventory *inventory ) {
    // These are from UIOverlayVertex
    vertex_buffer_layout_init( &inventory->UI.vbl );
    vertex_buffer_layout_push_float( &inventory->UI.vbl, 2 ); // UIOverlayVertex screen_position
    vertex_buffer_layout_push_float( &inventory->UI.vbl, 1 ); // UIOverlayVertex is_block
    vertex_buffer_layout_push_float( &inventory->UI.vbl, 4 ); // UIOverlayVertex tex_coord

    {
        *(&inventory->UI.screen_x) = 0;
        *(&inventory->UI.screen_y) = 0;

        int num_blocks = 1 * ISOMETRIC_FACES;
        int num_points_per_block = 4;
        int num_index_per_block = 18;

        int vb_size = num_blocks * num_points_per_block * ISOMETRIC_FACES;
        int ib_size = num_blocks * num_index_per_block;

        unsigned int *ib_data_inventory = ( unsigned int * )calloc( ib_size, sizeof( unsigned int ) );
        UIOverlayVertex *vb_data_inventory = ( UIOverlayVertex * )calloc( vb_size, sizeof( UIOverlayVertex ) );
        InventorySlot *inventory_slots = ( InventorySlot * )calloc( num_blocks, sizeof( InventorySlot ) );

        for ( int i_block = 0; i_block < num_blocks; i_block+=ISOMETRIC_FACES ) {
            int block_grid_coord_x = i_block % INVENTORY_BLOCKS_PER_ROW;
            int block_grid_coord_y = i_block / INVENTORY_BLOCKS_PER_ROW;
            int block_corner_x = *(&inventory->UI.screen_x) + block_grid_coord_x * INVENTORY_BLOCK_SPACING;
            int block_corner_y = *(&inventory->UI.screen_y) + block_grid_coord_y * INVENTORY_BLOCK_SPACING;

            unsigned int *ui_ib = &ib_data_inventory[ i_block * num_index_per_block ];
            // Top
            ui_ib[ 0 ] = i_block * num_points_per_block + 0;
            ui_ib[ 1 ] = i_block * num_points_per_block + 3;
            ui_ib[ 2 ] = i_block * num_points_per_block + 1;
            ui_ib[ 3 ] = i_block * num_points_per_block + 3;
            ui_ib[ 4 ] = i_block * num_points_per_block + 0;
            ui_ib[ 5 ] = i_block * num_points_per_block + 2;
            // Front
            ui_ib[ 6 ] = i_block * num_points_per_block + 4;
            ui_ib[ 7 ] = i_block * num_points_per_block + 7;
            ui_ib[ 8 ] = i_block * num_points_per_block + 5;
            ui_ib[ 9 ] = i_block * num_points_per_block + 7;
            ui_ib[ 10 ] = i_block * num_points_per_block + 4;
            ui_ib[ 11 ] = i_block * num_points_per_block + 6;
            // Right
            ui_ib[ 12 ] = i_block * num_points_per_block + 8;
            ui_ib[ 13 ] = i_block * num_points_per_block + 11;
            ui_ib[ 14 ] = i_block * num_points_per_block + 9;
            ui_ib[ 15 ] = i_block * num_points_per_block + 11;
            ui_ib[ 16 ] = i_block * num_points_per_block + 8;
            ui_ib[ 17 ] = i_block * num_points_per_block + 10;

            for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
                for ( int i_point = 0; i_point < num_points_per_block; i_point++ ) {
                    // Get the block definition to see if it should be displayed isometrically or not.
                    Block *block = block_definition_get_definition(( BlockID )( FURNACE_LIT ));
                    int vb_index = i_block * num_points_per_block * ISOMETRIC_FACES + i_point + num_points_per_block * face;
                    UIOverlayVertex *ui_vertex = &vb_data_inventory[ vb_index ];

                    ui_vertex->data.block.x = ( i_point / 2 );
                    ui_vertex->data.block.y = ( i_point % 2 );
                    ui_vertex->data.block.a = 1.0f;
                    ui_vertex->is_block = 1;

                    if (block->is_seethrough) {
                        ui_vertex->screen_x = block_corner_x + INVENTORY_BLOCK_SIZE * ui_vertex->data.block.x;
                        ui_vertex->screen_y = block_corner_y + INVENTORY_BLOCK_SIZE * ui_vertex->data.block.y;

                        ui_vertex->data.block.id = block->id - 1;
                    } else {
                        ui_vertex->screen_x = block_corner_x + INVENTORY_BLOCK_SIZE * inventory_iso_rotations[ i_point + num_points_per_block * face ][ 0 ];
                        ui_vertex->screen_y = block_corner_y + INVENTORY_BLOCK_SIZE * inventory_iso_rotations[ i_point + num_points_per_block * face ][ 1 ];

                        ui_vertex->data.block.id = (block->textures[ inventory_isometric_face[ face ] ] - 1);
                    }
                    inventory_slots->ui_vertex = ui_vertex;
                }
            }
        }
        *(&inventory->slots) = inventory_slots;

        index_buffer_init( &inventory->UI.ib );
        index_buffer_set_data( &inventory->UI.ib, ib_data_inventory, ib_size );
        vertex_buffer_init( &inventory->UI.vb );
        vertex_buffer_set_data( &inventory->UI.vb, vb_data_inventory, sizeof( UIOverlayVertex ) * vb_size );
        vertex_array_init( &inventory->UI.va );
        vertex_array_add_buffer( &inventory->UI.va, &inventory->UI.vb, &inventory->UI.vbl, 0, 0 );
        free( vb_data_inventory );
        free( ib_data_inventory );
    }

    shader_init( &inventory->UI.shader, &inventory_vertex, &inventory_fragment );
}
void inventory_draw( Inventory *inventory, Renderer *renderer, Texture *blocksTexture, InputState *input, glm::mat4 &mvp_ui ) {
    shader_set_uniform_mat4f( &inventory->UI.shader, "u_MVP", mvp_ui );
    shader_set_uniform1i( &inventory->UI.shader, "u_Texture", blocksTexture->slot );

    if ( input->inventory_open ) {
        renderer_draw( renderer, &inventory->UI.va, &inventory->UI.ib, &inventory->UI.shader, 1 );
    }
}

void inventory_cleanup( Inventory *inventory ) {
    free(inventory->slots);
}