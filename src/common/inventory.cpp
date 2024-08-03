#include "common/RepGame.hpp"
#include "common/inventory_renderer.hpp"

void Inventory::init( VertexBufferLayout *ui_overlay_vbl_vertex, VertexBufferLayout *ui_overlay_vbl_instance ) {
    this->inventory_renderer.init( ui_overlay_vbl_vertex, ui_overlay_vbl_instance );
    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        InventorySlot *slot = &this->slots[ i_slot ];
        // slot->block_id = AIR;
        slot->block_id = ( BlockID )( i_slot % 5 );
    }
    this->inventory_renderer.render( 100, 100, this->slots );
}

void Inventory::onScreenSizeChange( int width, int height ) {
    this->inventory_renderer.render( width, height, this->slots );
}

void Inventory::draw( Renderer *renderer, Texture *blocksTexture, const glm::mat4 &mvp_ui, Shader *shader ) {
    this->inventory_renderer.draw( renderer, blocksTexture, mvp_ui, shader );
}

int Inventory::findOpenSlot( ) {
    for ( int i_slot = 0; i_slot < INVENTORY_MAX_SIZE; i_slot++ ) {
        if ( this->slots[ i_slot ].block_id == AIR )
            return i_slot;
    }
    return -1;
}
bool Inventory::addBlock( BlockID blockId ) {
    auto it = this->blockId_to_slot_map.find( blockId );
    if ( it == this->blockId_to_slot_map.end( ) ) {
        // This is a new block
        int openSlot = this->findOpenSlot( );
        if ( openSlot == -1 ) {
            return false;
        }
        this->slots[ openSlot ].block_id = blockId;
    } else {
        // This is an existing block, we don't support stacking yet...
    }
    return true;
};

void Inventory::handleInput( InputState *inputState ) {
}

void Inventory::cleanup( ) {
    this->inventory_renderer.cleanup( );
}
