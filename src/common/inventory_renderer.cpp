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

    { 0 * 0.5f + 0.5f, 0 * 0.5f + 0.5f, { 1, 0 }, 1, ISO_FACE_TOP },     // 0
    { -1 * 0.5f + 0.5f, 0.5f * 0.5f + 0.5f, { 1, 1 }, 1, ISO_FACE_TOP }, // 1
    { 1 * 0.5f + 0.5f, 0.5f * 0.5f + 0.5f, { 0, 0 }, 1, ISO_FACE_TOP },  // 2
    { 0 * 0.5f + 0.5f, 1 * 0.5f + 0.5f, { 0, 1 }, 1, ISO_FACE_TOP },     // 3

    { -1 * 0.5f + 0.5f, -0.5f * 0.5f + 0.5f, { 1, 0 }, 1, ISO_FACE_FRONT }, // 4
    { -1 * 0.5f + 0.5f, 0.5f * 0.5f + 0.5f, { 1, 1 }, 1, ISO_FACE_FRONT },  // a
    { 0 * 0.5f + 0.5f, -1 * 0.5f + 0.5f, { 0, 0 }, 1, ISO_FACE_FRONT },     // 5
    { 0 * 0.5f + 0.5f, 0 * 0.5f + 0.5f, { 0, 1 }, 1, ISO_FACE_FRONT },      // b

    { 0 * 0.5f + 0.5f, -1 * 0.5f + 0.5f, { 1, 0 }, 1, ISO_FACE_RIGHT },    // c
    { 0 * 0.5f + 0.5f, 0 * 0.5f + 0.5f, { 1, 1 }, 1, ISO_FACE_RIGHT },     // d
    { 1 * 0.5f + 0.5f, -0.5f * 0.5f + 0.5f, { 0, 0 }, 1, ISO_FACE_RIGHT }, // 6
    { 1 * 0.5f + 0.5f, 0.5f * 0.5f + 0.5f, { 0, 1 }, 1, ISO_FACE_RIGHT },  // e

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
    this->render_chain_inventory_icons.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    this->render_chain_inventory_background.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );

    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        auto pair = this->render_chain_inventory_icons.create_instance( );
        this->slots_entities[ i_slot ] = pair.first;
    }
}

UIOverlayInstance &InventoryRenderer::init_background_gray_cell( float gray ) {
    return this->init_background_cell( gray, gray, gray, 1.0f );
}

UIOverlayInstance &InventoryRenderer::init_background_cell( float r, float g, float b, float a ) {
    auto pair = this->render_chain_inventory_background.create_instance( );
    UIOverlayInstance &ui_vertex = pair.second;
    ui_vertex.tint[ 0 ] = r;
    ui_vertex.tint[ 1 ] = g;
    ui_vertex.tint[ 2 ] = b;
    ui_vertex.tint[ 3 ] = a;
    ui_vertex.is_isometric = 0;
    ui_vertex.is_block = 0;
    return ui_vertex;
}

void InventoryRenderer::onSizeChange( int width, int height, InventorySlot *inventory_slots ) {
    this->inv_height = height * 0.8;
    this->inv_width = width * 0.8;

    // Max stride of each cell.
    int width_limit = this->inv_width / INVENTORY_BLOCKS_PER_ROW;
    int height_limit = this->inv_height / ( INVENTORY_BLOCKS_PER_COL * 2 );
    bool height_limited = height_limit < width_limit;
    if ( height_limited ) {
        this->item_stride_size = height_limit;
    } else {
        this->item_stride_size = width_limit;
    }

    this->inv_x = -this->inv_width / 2;
    this->inv_y = -this->inv_height / 2;

    this->inv_items_width = this->item_stride_size * INVENTORY_BLOCKS_PER_ROW;
    this->inv_items_height = this->item_stride_size * INVENTORY_BLOCKS_PER_COL;

    this->inv_items_x = this->inv_x - ( this->inv_items_width - this->inv_width ) / 2;
    // Position the inventory region so it's at the bottom, but add some extra pixels to make any rounding error horizontally
    // match the padding vertically.
    if ( height_limited ) {
        this->inv_items_y = this->inv_y;
    } else {
        this->inv_items_y = this->inv_y + ( ( this->inv_width - this->item_stride_size * INVENTORY_BLOCKS_PER_ROW ) / 2 );
    }

    float cell_fraction = 0.90;
    float block_fraction = 0.8;

    // this->inv_cell_size = this->item_stride_size * INVENTORY_BLOCKS_PER_ROW / ( INVENTORY_BLOCKS_PER_ROW + cell_fraction * ( INVENTORY_BLOCKS_PER_ROW + 1 ) );
    // this->inv_cell_size = this->item_stride_size * ( INVENTORY_BLOCKS_PER_ROW - cell_fraction * ( INVENTORY_BLOCKS_PER_ROW + 1 ) ) / INVENTORY_BLOCKS_PER_ROW;
    // this->inv_cell_size = this->item_stride_size * ( INVENTORY_BLOCKS_PER_ROW + cell_fraction - 1.0f ) / INVENTORY_BLOCKS_PER_ROW;
    // this->inv_cell_size = this->item_stride_size * ( INVENTORY_BLOCKS_PER_ROW - ( INVENTORY_BLOCKS_PER_ROW + 1 ) * ( 1.0f - cell_fraction ) ) / INVENTORY_BLOCKS_PER_ROW;
    // this->inv_cell_size = this->inv_items_width / ( INVENTORY_BLOCKS_PER_ROW + ( INVENTORY_BLOCKS_PER_ROW + 1 ) * ( 1.0f - cell_fraction ) );
    
    this->inv_cell_stride = this->inv_items_width / ( cell_fraction * INVENTORY_BLOCKS_PER_ROW + ( INVENTORY_BLOCKS_PER_ROW + 1 ) * ( 1.0f - cell_fraction ) );
    // this->inv_cell_stride = this->inv_items_height / ( cell_fraction * INVENTORY_BLOCKS_PER_COL + ( INVENTORY_BLOCKS_PER_COL + 1 ) * ( 1.0f - cell_fraction ) );
    this->inv_cell_size = this->inv_cell_stride * cell_fraction;
    this->inv_cell_offset = this->inv_cell_stride * ( 1.0f - cell_fraction );
    // this->inv_cell_offset = 0;

    this->inv_block_size = this->item_stride_size * block_fraction;
    this->inv_block_offset = ( this->item_stride_size - this->inv_block_size ) / 2;

    this->renderBackground( );
    this->fullItemRender( inventory_slots );
}
void InventoryRenderer::setSize( UIOverlayInstance &vertex, float left, float bottom, float width, float height ) {
    vertex.screen_x = left;
    vertex.screen_y = bottom;
    vertex.width = width;
    vertex.height = height;
}

void InventoryRenderer::reRenderSlot( int slot_index, InventorySlot &slot ) {
    entt::entity &slot_entity = this->slots_entities[ slot_index ];
    this->singleItemRender( slot_index, slot );
}

void InventoryRenderer::renderBackground( ) {
    // Coordinate system
    // 0,0 in the center of the screen.
    // X axis left to right
    // Y axis bottom to top
    this->render_chain_inventory_background.clear( );

    // Large background
    UIOverlayInstance &inv_bg = this->init_background_gray_cell( 0.1f );
    this->setSize( inv_bg, this->inv_x, this->inv_y, this->inv_width, this->inv_height );

    // Items background
    UIOverlayInstance &items_bg = this->init_background_gray_cell( 0.4f );
    this->setSize( items_bg, this->inv_items_x, this->inv_items_y, this->inv_items_width, this->inv_items_height );

    // Cells
    float cell_start_x = this->inv_items_x + this->inv_cell_offset;
    float cell_start_y = this->inv_items_y + this->inv_cell_offset;
    for ( int i = 0; i < INVENTORY_BLOCKS_PER_COL * INVENTORY_BLOCKS_PER_ROW; ++i ) {
        UIOverlayInstance &cell_bg = this->init_background_gray_cell( 0.5f );
        int block_grid_coord_x = i % INVENTORY_BLOCKS_PER_ROW;
        int block_grid_coord_y = i / INVENTORY_BLOCKS_PER_ROW;
        this->setSize( cell_bg,                                                   //
                       cell_start_x + block_grid_coord_x * this->inv_cell_stride, //
                       cell_start_y + block_grid_coord_y * this->inv_cell_stride, //
                       this->inv_cell_size,                                       //
                       this->inv_cell_size );
    }
}

void InventoryRenderer::singleItemRender( int slot_index, const InventorySlot &inventory_slot ) {
    entt::entity &slot_entity = this->slots_entities[ slot_index ];

    BlockID block_id = inventory_slot.block_id;
    if ( block_id == LAST_BLOCK_ID ) {
        if ( slot_entity != entt::null ) {
            this->render_chain_inventory_icons.remove( slot_entity );
        }
        slot_entity = entt::null;
        return;
    }
    Block *block = block_definition_get_definition( block_id );
    if ( block->renderOrder == RenderOrder_Transparent ) {
        if ( slot_entity != entt::null ) {
            this->render_chain_inventory_icons.remove( slot_entity );
        }
        slot_entity = entt::null;
        return;
    }
    UIOverlayInstance *ui_vertex_prt;
    if ( slot_entity == entt::null ) {
        auto pair = this->render_chain_inventory_icons.create_instance( );
        slot_entity = pair.first;
        ui_vertex_prt = &pair.second;
    } else {
        ui_vertex_prt = &this->render_chain_inventory_icons.get_instance( slot_entity );
    }
    UIOverlayInstance &ui_vertex = *ui_vertex_prt;

    int block_grid_coord_x = slot_index % INVENTORY_BLOCKS_PER_ROW;
    int block_grid_coord_y = slot_index / INVENTORY_BLOCKS_PER_ROW;
    int block_corner_x = this->inv_x + block_grid_coord_x * this->item_stride_size + this->inv_block_offset;
    int block_corner_y = this->inv_y + block_grid_coord_y * this->item_stride_size + this->inv_block_offset;

    float cell_x = ( slot_index / 2 );
    float cell_y = ( slot_index % 2 );
    ui_vertex.is_block = 1;

    ui_vertex.is_isometric = block->icon_is_isometric;
    ui_vertex.screen_x = block_corner_x;
    ui_vertex.screen_y = block_corner_y;
    ui_vertex.width = this->inv_block_size;
    ui_vertex.height = this->inv_block_size;

    for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
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
    }
    ui_vertex.tint[ 3 ] = 1;
    this->render_chain_inventory_icons.invalidate( slot_entity );
}

void InventoryRenderer::fullItemRender( InventorySlot *inventory_slots ) {
    for ( int slot_index = 0; slot_index < INVENTORY_MAX_SIZE; slot_index++ ) {
        const InventorySlot &inventory_slot = inventory_slots[ slot_index ];
        this->singleItemRender( slot_index, inventory_slot );
    }
}

void InventoryRenderer::draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader ) {
    showErrors( );

    glDisable( GL_DEPTH_TEST );
    this->render_chain_inventory_background.draw( renderer, shader );
    this->render_chain_inventory_icons.draw( renderer, shader );
    // pr_debug( "Drawing num icons:%ld", this->render_chain_inventory_icons.length( ) );

    glEnable( GL_DEPTH_TEST );
    glClear( GL_DEPTH_BUFFER_BIT );

    showErrors( );
}

void InventoryRenderer::cleanup( ) {
    this->render_chain_inventory_background.clear( );
    this->render_chain_inventory_icons.clear( );
}