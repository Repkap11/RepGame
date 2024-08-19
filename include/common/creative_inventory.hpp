#pragma once

#include "common/inventory_renderer.hpp"
#include "common/block_definitions.hpp"
#include <map>

class CreativeInventory {
    int width;
    int height;
    int num_blocks_max;
    InventorySlot *slots;
    int selected_page;
    void load_blocks_for_selected_page( );

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    void onScreenSizeChange( int width, int height );
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    void incrementSelectedPage( int offset );
    BlockID whichBlockClicked( int screen_x, int screen_y );
    void cleanup( );
};