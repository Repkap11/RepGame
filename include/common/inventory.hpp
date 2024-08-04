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

    int findOpenSlot( );

  public:
    InventoryRenderer inventory_renderer;
    void init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance, int width, int height );
    void onScreenSizeChange( int width, int height );
    bool addBlock( BlockID blockId );
    void handleInput( InputState *inputState );
    void draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader );
    void cleanup( );
};

#endif