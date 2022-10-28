#include "common/RepGame.hpp"
#include "common/inventory.hpp"
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
int NUM_POINTS_PER_BLOCK = 4;
int NUM_INDEXES_PER_BLOCK = 18;
int vb_size = INVENTORY_MAX_SIZE * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES;
int ib_size = INVENTORY_MAX_SIZE * NUM_INDEXES_PER_BLOCK * ISOMETRIC_FACES;
bool FIXED_INVENTORY = false;
void inventory_init( Inventory *inventory, VertexBufferLayout *ui_overlay_vbl ) {
    inventory->UI.ib_data_inventory = ( unsigned int * )calloc( ib_size, sizeof( unsigned int ) );
    inventory->UI.vb_data_inventory = ( UIOverlayVertex * )calloc( vb_size, sizeof( UIOverlayVertex ) );

    index_buffer_init( &inventory->UI.ib );
    vertex_buffer_init( &inventory->UI.vb );
    vertex_array_init( &inventory->UI.va );
    vertex_array_add_buffer( &inventory->UI.va, &inventory->UI.vb, ui_overlay_vbl, 0, 0 );

    int current_block_index = 0;
    int current_inventory_index = 0;
    if ( FIXED_INVENTORY ) {
        while ( current_inventory_index < INVENTORY_MAX_SIZE && current_block_index < BlockID::LAST_BLOCK_ID ) {
            Block *block;
            do {
                current_block_index++;
                block = block_definition_get_definition( ( BlockID )current_block_index );
            } while ( !block->is_pickable );
            if ( current_block_index >= BlockID::LAST_BLOCK_ID ) {
                continue;
            }
            InventorySlot *slot = &inventory->slots_items[ current_block_index ];
            pr_debug( "Using block: %d:%d", current_block_index, current_inventory_index );
            slot->block_id = block->id;
            slot->stack_count = 42;
            current_inventory_index++;
        }
    } else {
        inventory->slots_items[ 0 ].block_id = BlockID::DIRT;
        inventory->slots_items[ 1 ].block_id = BlockID::GRASS;
    }
}

void inventory_render( Inventory *inventory, int width, int height ) {
    int inv_height = height * 0.8;
    int inv_width = width * 0.8;

    inventory->UI.screen_x = -width / 2 + ( width - inv_width ) / 2;
    inventory->UI.screen_y = -height / 2 + ( height - inv_height ) / 2;
    int item_size = inv_width / INVENTORY_BLOCKS_PER_ROW;
    int icon_size = item_size * 0.8;
    int icon_offset = ( icon_size - item_size ) / 2;

    int shown_block_index = -1;

    int current_ib_index = 0;
    for ( int slot_index = 0; slot_index < INVENTORY_MAX_SIZE; slot_index++ ) {
        BlockID block_id = inventory->slots_items[ slot_index ].block_id;
        if ( block_id == BlockID::AIR ) {
            continue;
        }
        Block *block = block_definition_get_definition( block_id );

        InventorySlotMesh *inventory_slots_mesh = inventory->slots_mesh;
        int block_grid_coord_x = slot_index % INVENTORY_BLOCKS_PER_ROW;
        int block_grid_coord_y = slot_index / INVENTORY_BLOCKS_PER_ROW;
        int block_corner_x = inventory->UI.screen_x + block_grid_coord_x * item_size;
        int block_corner_y = inventory->UI.screen_y + ( INVENTORY_BLOCKS_PER_COL - block_grid_coord_y ) * item_size; // Put slot 0 at the upper (left) side.

        unsigned int *ui_ib = &inventory->UI.ib_data_inventory[ current_ib_index ];
        // Top
        ui_ib[ 0 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 0;
        ui_ib[ 1 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 3;
        ui_ib[ 2 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 1;
        ui_ib[ 3 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 3;
        ui_ib[ 4 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 0;
        ui_ib[ 5 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 2;
        // Front
        ui_ib[ 6 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 4;
        ui_ib[ 7 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 7;
        ui_ib[ 8 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 5;
        ui_ib[ 9 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 7;
        ui_ib[ 10 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 4;
        ui_ib[ 11 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 6;
        // Right
        ui_ib[ 12 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 8;
        ui_ib[ 13 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 11;
        ui_ib[ 14 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 9;
        ui_ib[ 15 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 11;
        ui_ib[ 16 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 8;
        ui_ib[ 17 ] = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + 10;

        for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
            for ( int i_point = 0; i_point < NUM_POINTS_PER_BLOCK; i_point++ ) {
                // Get the block definition to see if it should be displayed isometrically or not.
                int vb_index = slot_index * NUM_POINTS_PER_BLOCK * ISOMETRIC_FACES + i_point + NUM_POINTS_PER_BLOCK * face;
                UIOverlayVertex *ui_vertex = &inventory->UI.vb_data_inventory[ vb_index ];

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
                    ui_vertex->screen_x = block_corner_x + icon_size * ( verticies[ i_point + NUM_POINTS_PER_BLOCK * face ][ 0 ] / 2 + 0.5 );
                    ui_vertex->screen_y = block_corner_y + icon_size * ( verticies[ i_point + NUM_POINTS_PER_BLOCK * face ][ 1 ] / 2 + 0.5 );

                    ui_vertex->texture.id = ( block->textures[ inventory_isometric_face[ face ] ] - 1 );
                }
            }
        }
        inventory_slots_mesh->screen_x = block_corner_x;
        inventory_slots_mesh->screen_y = block_corner_y;
        inventory_slots_mesh->screen_x_end = block_corner_x + icon_size;
        inventory_slots_mesh->screen_y_end = block_corner_y + icon_size;
        inventory_slots_mesh->slot_pos = slot_index;

        current_ib_index += NUM_INDEXES_PER_BLOCK;
    }

    index_buffer_set_data( &inventory->UI.ib, inventory->UI.ib_data_inventory, ib_size );
    vertex_buffer_set_data( &inventory->UI.vb, inventory->UI.vb_data_inventory, sizeof( UIOverlayVertex ) * vb_size );
}

bool pos_within_slot( InventorySlotMesh *mesh, int x, int y ) {
    if ( x < mesh->screen_x ) {
        return false;
    }
    if ( y < mesh->screen_y ) {
        return false;
    }
    if ( x > mesh->screen_x_end ) {
        return false;
    }
    if ( y > mesh->screen_y_end ) {
        return false;
    }
    return true;
}
void inventory_process_inputs( Inventory *inventory, InputState *input ) {
    if ( input->mouse.buttons.left ) {
        int mouse_x = input->mouse.currentPosition.x;
        int mouse_y = input->mouse.currentPosition.y;
        for ( int i = 0; i < INVENTORY_MAX_SIZE; i++ ) {
            InventorySlotMesh *slot_mesh = &inventory->slots_mesh[ i ];
            InventorySlot slot = inventory->slots_items[ slot_mesh->slot_pos ];
            if ( slot.block_id == BlockID::AIR ) {
                continue;
            }
            if ( pos_within_slot( slot_mesh, mouse_x, mouse_y ) ) {
                pr_debug( "You clicked on:%d", slot.block_id );
            }
        }
    }
}

void inventory_draw( Inventory *inventory, Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader ) {
    showErrors( );
    renderer_draw( renderer, &inventory->UI.va, &inventory->UI.ib, shader, 1 );
    showErrors( );
}

void inventory_cleanup( Inventory *inventory ) {
    free( inventory->UI.vb_data_inventory );
    free( inventory->UI.ib_data_inventory );
}