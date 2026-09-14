#pragma once

#include "common/inventory_renderer.hpp"
#include "common/block_definitions.hpp"

class FontRenderer;

class SurvivalInventory {
    int width;
    int height;
    int num_slots;
    InventorySlot *slots;

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    void onScreenSizeChange( int width, int height );

    // Adds the given block to the inventory. Returns the number of items that
    // could not be added (0 = all added). If the inventory is full, the leftover
    // is returned so the caller can decide to drop it.
    int addBlock( BlockID blockId, int quantity );

    // Moves the entire stack from the given slot to the hotbar. Returns true if
    // the move succeeded.
    bool moveToHotbar( int slot_index, class Hotbar &hotbar );

    // Finds the given block in the inventory and moves the stack to the hotbar.
    // Returns true if the block was found and moved.
    bool moveBlockToHotbar( BlockID blockId, class Hotbar &hotbar );

    // Swaps the given slot's contents with the provided slot data (for
    // click-to-move within the inventory).
    void swapSlot( int slot_index, InventorySlot &held );

    // Minecraft-style click-to-pick-up / click-to-place. If held is empty,
    // picks up the slot's contents into held. If held has items, swaps held
    // with the slot's contents. Updates is_holding accordingly.
    void pickupOrSwapSlot( int slot_index, InventorySlot &held, bool &is_holding );

    // Returns a pointer to the slot data (for save/load and rendering).
    const InventorySlot *getSlots( ) const;
    InventorySlot *getSlotsMut( );

    // Applies saved slot data (for loading from save file).
    void applySavedInventory( const InventorySlot *savedSlots );
    void saveInventory( InventorySlot *savedSlots ) const;

    int whichSlotClicked( int screen_x, int screen_y ) const;
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    void drawQuantities( const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp );
    void cleanup( );
};
