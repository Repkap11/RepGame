#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"

void Inventory::init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance ) {
    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance );
}

void Inventory::onScreenSizeChange( int width, int height ) {
    this->inventory_renderer.render( width, height );
}

void Inventory::draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader ) {
    this->inventory_renderer.draw( renderer, blocksTexture, mvp_ui, shader );
}

void Inventory::handleInput( InputState *inputState ) {
}

void Inventory::cleanup( ) {
    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        if ( this->slots[ i_slot ] != NULL )
            free( this->slots[ i_slot ] );
    }
    this->inventory_renderer.cleanup( );
}
