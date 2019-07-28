#ifndef HEADER_INVENTORY_H
#define HEADER_INVENTORY_H

typedef struct {
    // Pointer slots;
} Inventory;

typedef struct {
    // int position;
    // ItemBlock item;
} InventorySlot;

void inventory_init( Inventory *inventory );
/**
 * Picks up an ItemBlock from the world and places it in the first available
 * InventorySlot (or stacks if applicable).
 * 
 * Returns true if there was an empty inventory slot, false otherwise.
 */
bool inventory_pickup( Inventory *inventory, ItemBlock *item );
/**
 * Drops an ItemBlock from the inventory to the world.
 */
void inventory_drop( Inventory *inventory, InventorySlot *item );
/**
 * Swaps to ItemBlocks in the inventory.
 */
void inventory_swap( Inventory *inventory, InventorySlot *up_loc, InventorySlot *down_loc );

void inventory_cleanup( Inventory *inventory );

#endif