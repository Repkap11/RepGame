#pragma once

#include "common/inventory_renderer.hpp"
#include "common/block_definitions.hpp"
#include <map>

class FontRenderer;

class Hotbar {
    int width;
    int height;
    int num_blocks_max;
    InventorySlot *slots;
    std::map<BlockID, int> blockId_to_slot_map;
    int selected_slot;

    int findOpenSlot( ) const;

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    void onScreenSizeChange( int width, int height );
    bool addBlock( bool alsoSelect, BlockID blockId );
    bool addBlockWithQuantity( BlockID blockId, int quantity );
    bool addBlockWithQuantity( BlockID blockId, int quantity, bool prefer_selected_slot );
    bool canPlaceSelected( ) const;
    bool consumeSelected( int amount );
    int getSelectedQuantity( ) const;
    void setSelectedSlot( int selected_slot );
    BlockID incrementSelectedSlot( int offset );
    BlockID getSelectedBlock( ) const;
    BlockID dropSelectedItem( );
    int getSelectedSlot( ) const;
    void applySavedInventory( const InventorySlot *savedSlots );
    void saveInventory( InventorySlot *savedSlots ) const;
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    void drawQuantities( const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp );
    void cleanup( );

    // Minecraft-style click-to-pick-up / click-to-place. Maintains the
    // blockId_to_slot_map so addBlock/addBlockWithQuantity stay consistent.
    void pickupOrSwapSlot( int slot_index, InventorySlot &held, bool &is_holding );

    // Moves the entire stack from the given hotbar slot to the survival
    // inventory. Returns true if the move succeeded.
    bool moveToSurvivalInventory( int slot_index, class SurvivalInventory &survival_inventory );
};