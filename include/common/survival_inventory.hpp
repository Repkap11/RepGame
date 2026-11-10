#pragma once

#include "common/inventory_renderer.hpp"
#include "common/item.hpp"

class FontRenderer;
struct UiRect;

class SurvivalInventory {
    friend void test_setup_inventories( struct RepGameState &gs );
    friend int test_item_entities( );
    int width;
    int height;
    int num_slots;
    InventorySlot *slots;

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    // Adopts the layout pass's items-slab rect and rebuilds the slot
    // background/icons (compute_layout must already have sized the grid).
    void apply_layout( const UiRect &slab );

    // Adds the given item to the inventory. Returns the number of items that
    // could not be added (0 = all added). If the inventory is full, the leftover
    // is returned so the caller can decide to drop it.
    int addItem( ItemID itemId, int quantity );

    // How many of the given item would fit in the inventory (stack space in
    // matching slots plus empty slots).
    int spaceFor( ItemID itemId ) const;

    // Removes `amount` of itemId from the first matching stack. True when a
    // stack had enough (bow ammo draw falls back here after the hotbar).
    bool consumeItem( ItemID itemId, int amount );

    // Moves the entire stack from the given slot to the hotbar. Returns true if
    // the move succeeded. prefer_selected_slot is forwarded to
    // Hotbar::addItemWithQuantity (fills the selected hotbar slot first).
    bool moveToHotbar( int slot_index, class Hotbar &hotbar, bool prefer_selected_slot );

    // Finds the given item in the inventory and moves the stack to the
    // hotbar, preferring non-full stacks over full ones. Returns true if the
    // item was found and moved.
    bool moveItemToHotbar( ItemID itemId, class Hotbar &hotbar, bool prefer_selected_slot );

    // Moves half of the given slot's stack (rounded up) to the hotbar.
    // Returns true if anything moved.
    bool moveHalfToHotbar( int slot_index, class Hotbar &hotbar );

    // Swaps the given slot's contents with the provided slot data (for
    // click-to-move within the inventory).
    void swapSlot( int slot_index, InventorySlot &held );

    // Minecraft-style click-to-pick-up / click-to-place. If held is empty,
    // picks up the slot's contents into held. If held has the same item type,
    // merges into the slot's stack up to its max stack with any leftover
    // staying in held. Otherwise swaps held with the slot's contents.
    // Updates is_holding accordingly.
    void pickupOrSwapSlot( int slot_index, InventorySlot &held, bool &is_holding );

    // Minecraft-style right-click. If held is empty, picks up half of the
    // slot's stack (rounded up). If held has items, places a single item into
    // the slot if it is empty or has the same item type with room.
    void rightClickSlot( int slot_index, InventorySlot &held, bool &is_holding );

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
