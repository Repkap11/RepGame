#pragma once

#include "common/inventory_renderer.hpp"
#include "common/item.hpp"

class FontRenderer;

class Hotbar {
    friend int test_mobs( );
    friend void test_setup_inventories( struct RepGameState &gs );
    friend int test_item_entities( );
    int width;
    int height;
    int num_blocks_max;
    InventorySlot *slots;
    int selected_slot;

    int findOpenSlot( ) const;

  public:
    InventoryRenderer inventory_renderer;
    void init( const VertexBufferLayout &ui_overlay_vbl_vertex, const VertexBufferLayout &ui_overlay_vbl_instance, int width, int height );
    void onScreenSizeChange( int width, int height );
    bool addItem( bool alsoSelect, ItemID itemId );
    bool addItemWithQuantity( ItemID itemId, int quantity );
    bool addItemWithQuantity( ItemID itemId, int quantity, bool prefer_selected_slot );
    bool canPlaceSelected( ) const;
    bool consumeSelected( int amount );
    // Removes `amount` of itemId from the first matching stack (any slot,
    // not just the selected one — bow ammo draw). True when a stack had
    // enough.
    bool consumeItem( ItemID itemId, int amount );
    int getSelectedQuantity( ) const;
    void setSelectedSlot( int selected_slot );
    ItemID incrementSelectedSlot( int offset );
    ItemID getSelectedItem( ) const;
    ItemID dropSelectedItem( );
    int getSelectedSlot( ) const;
    // Returns the index of a hotbar slot containing itemId, or -1 if none.
    // If require_non_full, only stacks below the item's max stack match.
    int findSlotWithItem( ItemID itemId, bool require_non_full ) const;
    // Returns how many more of itemId could fit on the hotbar (existing
    // non-full stacks plus empty slots).
    int spaceFor( ItemID itemId ) const;
    void applySavedInventory( const InventorySlot *savedSlots );
    void saveInventory( InventorySlot *savedSlots ) const;
    void draw( const Renderer &renderer, const Texture &blocksTexture, const Shader &shader );
    void drawQuantities( const Renderer &renderer, FontRenderer &font, const glm::mat4 &mvp );
    void cleanup( );

    // Minecraft-style click-to-pick-up / click-to-place. Merges onto matching
    // stacks instead of swapping.
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

    // Read/replace a slot's whole stack, keeping the
    // icon renderer consistent. Used by chest transfers.
    InventorySlot getSlotContents( int slot_index ) const;
    void setSlotContents( int slot_index, const InventorySlot &value );
};