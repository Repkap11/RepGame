#ifndef HEADER_INVENTORY_H
#define HEADER_INVENTORY_H

#include "common/inventory_renderer.hpp"
#include "common/block_definitions.hpp"
#include <map>

class Inventory {
    int width;
    int height;
    int num_blocks_max;
    InventorySlot *slots;
    std::map<BlockID, int> blockId_to_slot_map;
    int selected_slot;

    int findOpenSlot( );

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    void onScreenSizeChange( int width, int height );
    bool addBlock( bool alsoSelect, BlockID blockId );
    void setSelectedSlot( int selected_slot );
    BlockID incrementSelectedSlot( int offset );
    BlockID getSelectedBlock( );
    BlockID dropSelectedItem( );
    int getSelectedSlot( );
    void applySavedInventory( const InventorySlot *savedSlots );
    void saveInventory( InventorySlot *savedSlots );
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    void cleanup( );
};

#endif