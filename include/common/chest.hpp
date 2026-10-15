#pragma once

// Chest containers: a placed chest block owns a fixed-size inventory, stored
// per-chunk on the client and authoritatively on the server. This header is
// intentionally low-level (no renderer/GL types) so it can be shared by the
// client, the server, and the wire protocol.

#include <glm.hpp>
#include <map>

#include "common/block_definitions.hpp"
#include "common/constants.hpp"

#define CHEST_INVENTORY_WIDTH 9
#define CHEST_INVENTORY_HEIGHT 3
#define CHEST_SLOTS ( CHEST_INVENTORY_WIDTH * CHEST_INVENTORY_HEIGHT )

// glm::ivec3 map ordering, shared by the server's world cache, chest caches,
// and chest file IO. (Defined here rather than in server_logic.hpp so client
// and server share one definition.)
struct Compare_I_Vec3 {
    bool operator( )( const glm::ivec3 &a, const glm::ivec3 &b ) const {
        int x_diff = a.x - b.x;
        if ( x_diff != 0 ) {
            return x_diff < 0;
        }
        int y_diff = a.y - b.y;
        if ( y_diff != 0 ) {
            return y_diff < 0;
        }
        int z_diff = a.z - b.z;
        return z_diff < 0;
    }
};

// One chest's contents. Default-constructs to all-empty slots.
struct ChestContents {
    InventorySlot slots[ CHEST_SLOTS ];

    ChestContents( ) {
        clear( );
    }

    void clear( ) {
        for ( int i = 0; i < CHEST_SLOTS; i++ ) {
            slots[ i ].block_id = LAST_BLOCK_ID;
            slots[ i ].quantity = 0;
        }
    }
};

// Chests are identified by block id, not a Block-definition flag, so the
// server (which never initializes the GL-backed block_definitions table) can
// answer the same question. Only the single chest is a container; the
// DOUBLE_CHEST_* halves stay decorative.
static inline bool block_is_chest( BlockID id ) {
    return id == SINGLE_CHEST_LATCH;
}

// Chest contents keyed by world block position (file + server form). The
// client-side Chunk instead keys by in-chunk block index.
typedef std::map<glm::ivec3, ChestContents, Compare_I_Vec3> ChestContentsMap;
