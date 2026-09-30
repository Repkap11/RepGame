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
    // Returns the index of a hotbar slot containing blockId, or -1 if none.
    // If require_non_full, only stacks below MAX_STACK_SIZE match.
    int findSlotWithBlock( BlockID blockId, bool require_non_full ) const;
    // Returns how many more of blockId could fit on the hotbar (existing
    // non-full stacks plus empty slots).
    int spaceFor( BlockID blockId ) const;
    void applySavedInventory( const InventorySlot *savedSlots );
    void saveInventory( InventorySlot *savedSlots ) const;
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    void drawQuantities( const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp );
    void cleanup( );

    // Minecraft-style click-to-pick-up / click-to-place. Merges onto matching
    // stacks instead of swapping. Maintains the blockId_to_slot_map so
    // addBlock/addBlockWithQuantity stay consistent.
    void pickupOrSwapSlot( int slot_index, InventorySlot &held, bool &is_holding );

    // Minecraft-style right-click: picks up half of the slot's stack (rounded
    // up), or places a single held block if the slot accepts it.
    void rightClickSlot( int slot_index, InventorySlot &held, bool &is_holding );

    // Moves the entire stack from the given hotbar slot to the survival
    // inventory. Returns true if the move succeeded.
    bool moveToSurvivalInventory( int slot_index, class SurvivalInventory &survival_inventory );

    // Moves half of the given hotbar slot's stack (rounded up) to the survival
    // inventory. Returns true if anything moved.
    bool moveHalfToSurvivalInventory( int slot_index, class SurvivalInventory &survival_inventory );
};