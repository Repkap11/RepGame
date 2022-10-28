#ifndef HEADER_INVENTORY_H
#define HEADER_INVENTORY_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "RepGame.hpp"
#include "input.hpp"
#include "ui_overlay.hpp"

#define INVENTORY_BLOCKS_PER_ROW 20
#define INVENTORY_BLOCKS_PER_COL 5
#define INVENTORY_MAX_SIZE ( INVENTORY_BLOCKS_PER_ROW * INVENTORY_BLOCKS_PER_COL )

// #define INVENTORY_BLOCK_SIZE 30
// #define INVENTORY_BLOCK_SPACING 80

typedef struct {
    int slot_pos;
    float screen_x;
    float screen_y;
} InventorySlotMesh;

typedef struct {
    int stack_count;
    BlockID block_id;
} InventorySlot;

typedef struct {
    InventorySlot slots_items[ INVENTORY_MAX_SIZE ];
    InventorySlotMesh slots_mesh[ INVENTORY_MAX_SIZE ];
    struct {
        int screen_x;
        int screen_y;
        int text_size;
        int text_spacing;

        IndexBuffer ib;
        VertexArray va;
        VertexBuffer vb;

        unsigned int *ib_data_inventory;
        UIOverlayVertex *vb_data_inventory;
    } UI;
} Inventory;

void inventory_init( Inventory *inventory, VertexBufferLayout *ui_overlay_vbl );
void inventory_render( Inventory *inventory, int width, int height );
void inventory_draw( Inventory *inventory, Renderer *renderer, Texture *blocksTexture, InputState *input, const glm::mat4 &mvp_ui, Shader *shader );
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