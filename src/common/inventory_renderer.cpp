#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"
#include "common/renderer/shader.hpp"

#define ISOMETRIC_FACES 3

#define VB_ISOMETRIC_QUAD_SIZE 16
static const UIOverlayVertex vb_isometric_quad[ VB_ISOMETRIC_QUAD_SIZE ] = {
    // Isometric
    { 0.5f, 0.625, { 1, 0 }, 1, ISO_FACE_TOP },   // 0
    { 0.0f, 0.8125f, { 1, 1 }, 1, ISO_FACE_TOP }, // 1
    { 1.0f, 0.8125f, { 0, 0 }, 1, ISO_FACE_TOP }, // 2
    { 0.5f, 1.0f, { 0, 1 }, 1, ISO_FACE_TOP },    // 3

    { 0.0f, 0.1875f, { 1, 0 }, 1, ISO_FACE_FRONT }, // 4
    { 0.0f, 0.8125f, { 1, 1 }, 1, ISO_FACE_FRONT }, // a
    { 0.5f, 0.0f, { 0, 0 }, 1, ISO_FACE_FRONT },    // 5
    { 0.5f, 0.625, { 0, 1 }, 1, ISO_FACE_FRONT },   // b

    { 0.5f, 0.0f, { 1, 0 }, 1, ISO_FACE_RIGHT },    // c
    { 0.5f, 0.625, { 1, 1 }, 1, ISO_FACE_RIGHT },   // d
    { 1.0f, 0.1875f, { 0, 0 }, 1, ISO_FACE_RIGHT }, // 6
    { 1.0f, 0.8125f, { 0, 1 }, 1, ISO_FACE_RIGHT }, // e

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
int num_points_per_block = 4;
int num_index_per_block = 18;

void InventoryRenderer::init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height ) {
    this->width = width;
    this->height = height;
    this->num_blocks_max = width * height;
    this->slots_entities = static_cast<entt::entity *>( calloc( this->num_blocks_max, sizeof( entt::entity ) ) );

    this->vb_max_size = this->num_blocks_max * num_points_per_block * ISOMETRIC_FACES;
    this->ib_max_size = this->num_blocks_max * num_index_per_block * ISOMETRIC_FACES;

    this->selected_slot_bg = entt::null;
    this->selected_slot_index = -1;

    this->render_chain_inventory_icons.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );
    this->render_chain_inventory_background.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance, vb_isometric_quad, VB_ISOMETRIC_QUAD_SIZE, ib_isometric_quad, IB_ISOMETRIC_QUAD_SIZE );

    for ( int i_slot = 0; i_slot < this->num_blocks_max; i_slot++ ) {
        // auto pair = this->render_chain_inventory_icons.create_instance( );
        // this->slots_entities[ i_slot ] = pair.first;
        this->slots_entities[ i_slot ] = entt::null;
    }
}

UIOverlayInstance &InventoryRenderer::init_background_gray_cell( float gray ) {
    return this->init_background_cell( gray, gray, gray, 1.0f );
}

UIOverlayInstance &InventoryRenderer::init_background_cell( const float r, const float g, const float b, const float a ) {
    auto [fst, snd ] = this->render_chain_inventory_background.create_instance( );
    UIOverlayInstance &ui_vertex = snd;
    ui_vertex.tint[ 0 ] = r;
    ui_vertex.tint[ 1 ] = g;
    ui_vertex.tint[ 2 ] = b;
    ui_vertex.tint[ 3 ] = a;
    ui_vertex.is_isometric = 0;
    ui_vertex.is_block = 0;
    return ui_vertex;
}

void InventoryRenderer::onSizeChange( const int screen_width, const int screen_height, const InventorySlot *inventory_slots ) {
    this->screen_width = screen_width;
    this->screen_height = screen_height;
    this->inv_height = this->screen_height * this->options.max_height_percent;
    this->inv_width = this->screen_width * this->options.max_width_percent;

    this->inv_x = -this->inv_width / 2;
    if ( this->options.gravity_bottom ) {
        this->inv_y = -this->screen_height / 2;
    } else {
        this->inv_y = -this->inv_height / 2;
    }

    constexpr float cell_fraction = 0.9;
    // Max stride of each cell.
    const int width_limit = this->inv_width / this->width;
    const int height_limit = this->inv_height * this->options.active_height_percent / this->height;
    const bool height_limited = height_limit < width_limit;
    if ( height_limited ) {
        this->inv_items_height = height_limit * this->height;
        this->inv_cell_stride = this->inv_items_height / ( cell_fraction * this->height + ( this->height + 1 ) * ( 1.0f - cell_fraction ) );
        this->inv_cell_size = this->inv_cell_stride * cell_fraction;
        this->inv_cell_offset = this->inv_cell_stride * ( 1.0f - cell_fraction );
        this->inv_items_width = inv_cell_stride * this->width + this->inv_cell_offset;
    } else {
        this->inv_items_width = width_limit * this->width;
        this->inv_cell_stride = this->inv_items_width / ( cell_fraction * this->width + ( this->width + 1 ) * ( 1.0f - cell_fraction ) );
        this->inv_cell_size = this->inv_cell_stride * cell_fraction;
        this->inv_cell_offset = this->inv_cell_stride * ( 1.0f - cell_fraction );
        this->inv_items_height = inv_cell_stride * this->height + this->inv_cell_offset;
    }
    this->inv_items_x = this->inv_x - ( this->inv_items_width - this->inv_width ) / 2;
    if ( height_limited || this->options.gravity_bottom ) {
        this->inv_items_y = this->inv_y;
    } else {
        // This adjusts the bottom padding to match the horizontal padding when the horizontal padding has accumulated rounding error.
        this->inv_items_y = this->inv_y + ( ( this->inv_width - inv_items_width ) / 2 );
    }

    float block_fraction = 0.9;
    this->inv_block_size = this->inv_cell_size * block_fraction;
    this->inv_block_offset = this->inv_cell_offset + ( this->inv_cell_size - this->inv_block_size ) / 2;

    float selected_fraction = 1.15;
    this->inv_selected_size = this->inv_cell_size * selected_fraction;
    this->inv_selected_offset = this->inv_cell_offset + ( this->inv_cell_size - this->inv_selected_size ) / 2;

    this->renderBackground( );
    this->fullItemRender( inventory_slots );
}

void InventoryRenderer::setSelectedSlot( int slot_index ) {
    if ( slot_index == this->selected_slot_index ) {
        return;
    }
    this->selected_slot_index = slot_index;
    this->selectedSlotRender( );
}

void InventoryRenderer::selectedSlotRender( ) {
    if ( !this->options.shows_selection_slot ) {
        return;
    }
    if ( this->selected_slot_index == -1 ) {
        if ( this->selected_slot_bg != entt::null ) {
            this->render_chain_inventory_background.remove( this->selected_slot_bg );
            this->selected_slot_bg = entt::null;
        }
        return;
    }

    UIOverlayInstance *ui_vertex_prt;
    bool needsVertexInit = this->selected_slot_bg == entt::null;
    if ( needsVertexInit ) {
        auto pair = this->render_chain_inventory_background.create_instance( );
        this->selected_slot_bg = pair.first;
        ui_vertex_prt = &pair.second;
    } else {
        ui_vertex_prt = &this->render_chain_inventory_background.get_instance( this->selected_slot_bg );
    }
    UIOverlayInstance &ui_vertex = *ui_vertex_prt;
    if ( needsVertexInit ) {
        float gray_color = 0.6f;
        ui_vertex.tint[ 0 ] = gray_color;
        ui_vertex.tint[ 1 ] = gray_color;
        ui_vertex.tint[ 2 ] = gray_color;
        ui_vertex.tint[ 3 ] = 1.0f;
        ui_vertex.is_isometric = 0;
        ui_vertex.is_block = 0;
        ui_vertex.width = this->inv_selected_size;
        ui_vertex.height = this->inv_selected_size;
    }
    int block_grid_coord_x = this->selected_slot_index % this->width;
    int block_grid_coord_y = this->selected_slot_index / this->width;
    ui_vertex.screen_x = this->inv_items_x + block_grid_coord_x * this->inv_cell_stride + this->inv_selected_offset;
    ui_vertex.screen_y = this->inv_items_y + block_grid_coord_y * this->inv_cell_stride + this->inv_selected_offset;
    ui_vertex.screen_z = ORDER_Z_INV_SELECTED_SLOT;

    this->render_chain_inventory_background.invalidate( this->selected_slot_bg );
}

void InventoryRenderer::setSize( UIOverlayInstance &vertex, float left, float bottom, float width, float height, float order_z ) {
    vertex.screen_x = left;
    vertex.screen_y = bottom;
    vertex.screen_z = order_z;
    vertex.width = width;
    vertex.height = height;
}

void InventoryRenderer::changeSlotItem( int slot_index, const InventorySlot &slot ) {
    this->singleItemRender( slot_index, slot );
}

int InventoryRenderer::whichSlotClicked( int screen_x, int screen_y ) const {
    // pr_debug( "adjusted_pos:  %d %d", this->screen_width, this->screen_height );

    screen_x = screen_x - this->screen_width / 2;
    screen_y = screen_y - this->screen_height / 2;
    screen_y = -screen_y;
    // pr_debug( "adjusted_pos:  %d %d", screen_x, screen_y );

    // pr_debug( "inv_items:  %d %d", this->inv_items_x, this->inv_items_y );

    const int cell_start_x = this->inv_items_x + this->inv_cell_offset;
    const int cell_start_y = this->inv_items_y + this->inv_cell_offset;
    const int cell_offset_x = screen_x - cell_start_x;
    const int cell_offset_y = screen_y - cell_start_y;
    if ( cell_offset_x < 0 || cell_offset_y < 0 ) {
        // pr_debug( "Coord too low" );
        return -1;
    }
    const int block_grid_coord_x = cell_offset_x / this->inv_cell_stride;
    const int block_grid_coord_y = cell_offset_y / this->inv_cell_stride;

    const float remaining_x = cell_offset_x - block_grid_coord_x * this->inv_cell_stride;
    const float remaining_y = cell_offset_y - block_grid_coord_y * this->inv_cell_stride;
    if ( remaining_x > this->inv_cell_size || remaining_y > this->inv_cell_size ) {
        // pr_debug( "Coord goes into border" );
        return -1;
    }

    if ( block_grid_coord_x > this->width || block_grid_coord_y > this->height ) {
        // pr_debug( "Coord too high %d %d", block_grid_coord_x, block_grid_coord_y );
        return -1;
    }
    int slot_index = block_grid_coord_x + ( block_grid_coord_y * this->width );
    // pr_debug( "Found slot:%d %d   %d", block_grid_coord_x, block_grid_coord_y, slot_index );
    return slot_index;
}

void InventoryRenderer::renderBackground( ) {
    // Coordinate system
    // 0,0 in the center of the screen.
    // X axis left to right
    // Y axis bottom to top
    this->render_chain_inventory_background.clear( );
    this->selected_slot_bg = entt::null;

    // Large background
    // UIOverlayInstance &inv_bg = this->init_background_gray_cell( 0.1f );
    // setSize( inv_bg, this->inv_x, this->inv_y, this->inv_width, this->inv_height );

    // Items background
    UIOverlayInstance &items_bg = this->init_background_gray_cell( 0.4f );
    setSize( items_bg, this->inv_items_x, this->inv_items_y, this->inv_items_width, this->inv_items_height, ORDER_Z_INV_ITEMS_BG );

    // Selected slot
    this->selectedSlotRender( );

    // Cells
    float cell_start_x = this->inv_items_x + this->inv_cell_offset;
    float cell_start_y = this->inv_items_y + this->inv_cell_offset;
    for ( int i = 0; i < this->height * this->width; ++i ) {
        UIOverlayInstance &cell_bg = this->init_background_gray_cell( 0.5f );
        int block_grid_coord_x = i % this->width;
        int block_grid_coord_y = i / this->width;
        setSize( cell_bg,                                                   //
                 cell_start_x + block_grid_coord_x * this->inv_cell_stride, //
                 cell_start_y + block_grid_coord_y * this->inv_cell_stride, //
                 this->inv_cell_size,                                       //
                 this->inv_cell_size,                                       //
                 ORDER_Z_INV_SLOT_BG );
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
    bool needsVertexInit = slot_entity == entt::null;
    if ( needsVertexInit ) {
        auto pair = this->render_chain_inventory_icons.create_instance( );
        slot_entity = pair.first;
        ui_vertex_prt = &pair.second;
    } else {
        ui_vertex_prt = &this->render_chain_inventory_icons.get_instance( slot_entity );
    }
    UIOverlayInstance &ui_vertex = *ui_vertex_prt;
    if ( needsVertexInit ) {
        ui_vertex.screen_z = ORDER_Z_INV_BLOCKS;
        ui_vertex.is_block = 1;
    }

    ui_vertex.width = this->inv_block_size;
    ui_vertex.height = this->inv_block_size;
    int block_grid_coord_x = slot_index % this->width;
    int block_grid_coord_y = slot_index / this->width;

    ui_vertex.screen_x = this->inv_items_x + block_grid_coord_x * this->inv_cell_stride + this->inv_block_offset;
    ui_vertex.screen_y = this->inv_items_y + block_grid_coord_y * this->inv_cell_stride + this->inv_block_offset;

    ui_vertex.is_isometric = block->icon_is_isometric;

    for ( int i = 0; i < 4; i++ ) {
        // set RGBA tint to no tint
        ui_vertex.tint[ i ] = 1.0f;
    }

    for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
        if ( !block->icon_is_isometric ) {
            // Like Reeds
            ui_vertex.id_isos[ face ] = block->inventory_non_isometric_id - 1;
        } else {
            // Like grass
            ui_vertex.id_isos[ face ] = ( block->textures[ inventory_isometric_face[ face ] ] - 1 );
        }
    }
    this->render_chain_inventory_icons.invalidate( slot_entity );
}

void InventoryRenderer::fullItemRender( const InventorySlot *inventory_slots ) {
    for ( int slot_index = 0; slot_index < this->num_blocks_max; slot_index++ ) {
        const InventorySlot &inventory_slot = inventory_slots[ slot_index ];
        this->singleItemRender( slot_index, inventory_slot );
    }
}

void InventoryRenderer::draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader ) {
    showErrors( );

    // glDisable( GL_DEPTH_TEST );
    glEnable( GL_DEPTH_TEST );
    glClear( GL_DEPTH_BUFFER_BIT );
    this->render_chain_inventory_background.draw( renderer, shader );
    this->render_chain_inventory_icons.draw( renderer, shader );
    // pr_debug( "Drawing num background:%d", this->render_chain_inventory_background.length( ) );
    // pr_debug( "Drawing num icons:%ld", this->render_chain_inventory_icons.length( ) );
    // glClear( GL_DEPTH_BUFFER_BIT );

    showErrors( );
}

void InventoryRenderer::cleanup( ) {
    this->render_chain_inventory_background.clear( );
    this->selected_slot_bg = entt::null;
    this->render_chain_inventory_icons.clear( );
}