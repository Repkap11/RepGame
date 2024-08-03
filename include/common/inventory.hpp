#ifndef HEADER_INVENTORY_H
#define HEADER_INVENTORY_H

#include "common/inventory_renderer.hpp"

class Inventory {
    InventoryRenderer inventory_renderer;
    InventorySlot slots[ INVENTORY_MAX_SIZE ];

  public:
    void init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance );
    void onScreenSizeChange( int width, int height );
    void handleInput( InputState *inputState );
    void draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader );
    void cleanup( );
};

#endif