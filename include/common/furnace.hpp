#pragma once

// Furnace containers: a placed furnace block owns a fixed-size inventory
// (input/fuel/output slots) plus smelt progress, stored per-chunk on the
// client and authoritatively on the server. This header is intentionally
// low-level (no renderer/GL types) so it can be shared by the client, the
// server, and the wire protocol.

#include <glm.hpp>
#include <map>

#include "common/block_definitions.hpp"
#include "common/constants.hpp"
#include "common/chest.hpp" // Compare_I_Vec3

#define FURNACE_INVENTORY_SLOTS 3
#define FURNACE_SLOT_INPUT 0
#define FURNACE_SLOT_FUEL 1
#define FURNACE_SLOT_OUTPUT 2

// One furnace's contents and progress. Default-constructs to empty.
struct FurnaceContents {
    InventorySlot slots[ FURNACE_INVENTORY_SLOTS ];
    // Ticks (at UPS_RATE) spent on the current input item; resets to 0 when
    // a smelt completes. Meaningless while nothing is smelting.
    int cook_progress;
    // Remaining fuel burn time in ticks; >0 while the furnace is lit.
    int burn_ticks;

    FurnaceContents( ) {
        clear( );
    }

    void clear( ) {
        for ( int i = 0; i < FURNACE_INVENTORY_SLOTS; i++ ) {
            slots[ i ].item_id = LAST_BLOCK_ID;
            slots[ i ].quantity = 0;
        }
        cook_progress = 0;
        burn_ticks = 0;
    }
};

// Furnaces are identified by block id, not a Block-definition flag, so the
// server (which never initializes the GL-backed block_definitions table)
// can answer the same question. FURNACE and LIT_FURNACE are the same
// container; the lit variant only changes the rendered block + light.
static inline bool block_is_furnace( BlockID id ) {
    return id == FURNACE || id == LIT_FURNACE;
}

// Furnace contents keyed by world block position (file + server form). The
// client-side Chunk instead keys by in-chunk block index.
typedef std::map<glm::ivec3, FurnaceContents, Compare_I_Vec3> FurnaceContentsMap;

// Advances one furnace by the caller's tick cadence (implemented in
// src/common/furnace.cpp). Returns true when the lit state may have flipped
// so the caller can swap FURNACE<->LIT_FURNACE; `changed` reports any
// contents/progress mutation (for dirty-marking and re-broadcast).
bool furnace_tick( FurnaceContents &f, bool *changed );
