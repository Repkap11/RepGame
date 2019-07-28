#ifndef HEADER_INVENTORY_H
#define HEADER_INVENTORY_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "RepGame.hpp"
#include "input.hpp"

#define INVENTORY_MAX_SIZE 5
#define INVENTORY_BLOCKS_PER_ROW 20
#define INVENTORY_BLOCK_SIZE 50
#define INVENTORY_BLOCK_SPACING 60

typedef struct {
    int stack;
    int slot_pos;
    int is_active;
    float screen_x;
    float screen_y;
    BlockID block_id;
} InventorySlot;

typedef struct {
    InventorySlot *slots[ INVENTORY_MAX_SIZE ];
    unsigned int size;

    struct {
        int screen_x;
        int screen_y;
        int text_size;
        int text_spacing;

        VertexBufferLayout vbl;
        Shader shader;

        IndexBuffer ib;
        VertexArray va;
        VertexBuffer vb;
    } UI;
} Inventory;

void inventory_init( Inventory *inventory );
void inventory_draw( Inventory *inventory, Renderer *renderer, Texture *blocksTexture, InputState *input, glm::mat4 &mvp_ui );
/**
 * Picks up an ItemBlock from the world and places it in the first available
 * InventorySlot (or stacks if applicable).
 * 
 * Returns true if there was an empty inventory slot, false otherwise.
 */
// bool inventory_pickup( Inventory *inventory, ItemBlock *item );
/**
 * Drops an ItemBlock from the inventory to the world.
 */
// void inventory_drop( Inventory *inventory, InventorySlot *item );
/**
 * Swaps to ItemBlocks in the inventory.
 */
// void inventory_swap( Inventory *inventory, InventorySlot *up_loc, InventorySlot *down_loc );

void inventory_cleanup( Inventory *inventory );

#endif