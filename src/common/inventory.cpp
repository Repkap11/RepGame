#include "common/RepGame.hpp"
#include "common/inventory.hpp"
#include "common/abstract/shader.hpp"

#define ISOMETRIC_FACES 3

float verticies[][ 2 ] = {
    // Top
    { 0, 0 },      // 0
    { -1, 0.5f },  // 1
    { 1, 0.5f },   // 2
    { 0, 1 },      // 3
    // Front
    { -1, -0.5f }, // 4
    { -1, 0.5f },
    { 0, -1 },     // 5
    { 0, 0 },
    // Right
    { 0, -1 },
    { 0, 0 },
    { 1, -0.5f },  // 6
    { 1, 0.5f },
};

// unsigned int indicies[] {
//     // Top
//     2, 3, 0,
//     3, 1, 0,
//     // Front
//     4, 5, 0,
//     0, 1, 4,
//     // Right
//     5, 6, 0,
//     6, 2, 0,
// };

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

        int num_blocks = 5;
        int num_points_per_block = 4;
        int num_index_per_block = 18;

        int vb_size = num_blocks * num_points_per_block * ISOMETRIC_FACES;
        int ib_size = num_blocks * num_index_per_block * ISOMETRIC_FACES;

        unsigned int *ib_data_inventory = ( unsigned int * )calloc( ib_size, sizeof( unsigned int ) );
        UIOverlayVertex *vb_data_inventory = ( UIOverlayVertex * )calloc( vb_size, sizeof( UIOverlayVertex ) );

        for ( int i_block = 0; i_block < num_blocks; i_block++ ) {
            InventorySlot *inventory_slots = ( InventorySlot * )malloc( sizeof( InventorySlot ) );
            int block_grid_coord_x = i_block % INVENTORY_BLOCKS_PER_ROW;
            int block_grid_coord_y = i_block / INVENTORY_BLOCKS_PER_ROW;
            int block_corner_x = *(&inventory->UI.screen_x) + block_grid_coord_x;
            int block_corner_y = *(&inventory->UI.screen_y) + block_grid_coord_y;

            unsigned int *ui_ib = &ib_data_inventory[ i_block * num_index_per_block ];
            // Top
            ui_ib[ 0 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 0;
            ui_ib[ 1 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 3;
            ui_ib[ 2 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 1;
            ui_ib[ 3 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 3;
            ui_ib[ 4 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 0;
            ui_ib[ 5 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 2;
            // Front
            ui_ib[ 6 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 4;
            ui_ib[ 7 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 7;
            ui_ib[ 8 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 5;
            ui_ib[ 9 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 7;
            ui_ib[ 10 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 4;
            ui_ib[ 11 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 6;
            // Right
            ui_ib[ 12 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 8;
            ui_ib[ 13 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 11;
            ui_ib[ 14 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 9;
            ui_ib[ 15 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 11;
            ui_ib[ 16 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 8;
            ui_ib[ 17 ] = i_block * num_points_per_block * ISOMETRIC_FACES + 10;

            for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
                for ( int i_point = 0; i_point < num_points_per_block; i_point++ ) {
                    // Get the block definition to see if it should be displayed isometrically or not.
                    Block *block = block_definition_get_definition(( BlockID )( TNT ));
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
                        ui_vertex->screen_x = block_corner_x + INVENTORY_BLOCK_SIZE * verticies[ i_point + num_points_per_block * face ][ 0 ];
                        ui_vertex->screen_y = block_corner_y + INVENTORY_BLOCK_SIZE * verticies[ i_point + num_points_per_block * face ][ 1 ];

                        ui_vertex->data.block.id = (block->textures[ inventory_isometric_face[ face ] ] - 1);
                    }

                }
            }
            inventory_slots->screen_x = block_corner_x;
            inventory_slots->screen_y = block_corner_y;
            inventory_slots->slot_pos = i_block;
            inventory_slots->stack = 0;
            inventory_slots->is_active = false;
        }

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
        if ( input->mouse.buttons.left ) {
            printf("Clicked!\n");
        }
        // for ( int i_pos = 0; i_pos < 12; i_pos++ ) {
        // glBufferSubData( GL_ARRAY_BUFFER, sizeof( UIOverlayVertex ), sizeof( UIOverlayVertex ), )
        // }
        showErrors();
        renderer_draw( renderer, &inventory->UI.va, &inventory->UI.ib, &inventory->UI.shader, 1 );
        showErrors();
    }
}

void inventory_cleanup( Inventory *inventory ) {
    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        if ( inventory->slots[ i_slot ] != NULL )
            free(inventory->slots[ i_slot ]);
    }
}