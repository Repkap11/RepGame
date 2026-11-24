#pragma once

// Chest containers: a placed chest block owns a fixed-size inventory, stored
// per-chunk on the client and authoritatively on the server. This header is
// intentionally low-level (no renderer/GL types) so it can be shared by the
// client, the server, and the wire protocol.

#include <glm/glm.hpp>
#include <map>

#include "common/block_definitions.hpp"
#include "common/constants.hpp"

#define CHEST_INVENTORY_WIDTH 9
#define CHEST_INVENTORY_HEIGHT 3
#define CHEST_SLOTS ( CHEST_INVENTORY_WIDTH * CHEST_INVENTORY_HEIGHT )
// A merged pair composes two per-half inventories into one 54-slot view.
#define DOUBLE_CHEST_SLOTS ( 2 * CHEST_SLOTS )

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
            slots[ i ].item_id = LAST_BLOCK_ID;
            slots[ i ].quantity = 0;
        }
    }
};

// Chests are identified by block id, not a Block-definition flag, so the
// server (which never initializes the GL-backed block_definitions table) can
// answer the same question. The DOUBLE_CHEST_* halves count too: each owns
// its own 27-slot inventory under its own position, and a consistent
// adjacent pair opens as one merged 54-slot panel.
static inline bool block_is_chest( BlockID id ) {
    return id == CHEST || id == DOUBLE_CHEST_LEFT_LATCH || id == DOUBLE_CHEST_RIGHT_LATCH;
}

// A block id that can only exist as one cell of a merged pair (or an
// orphan waiting to revert to CHEST).
static inline bool block_is_chest_half( BlockID id ) {
    return id == DOUBLE_CHEST_LEFT_LATCH || id == DOUBLE_CHEST_RIGHT_LATCH;
}

// Double-chest pair geometry. The two halves share a yaw rotation and sit
// side by side on the axis perpendicular to the front: at rotation 0 the
// def's FACE_FRONT (+z) faces the placer, so rotation r's lateral axis is
// fixed. chest_pair_left_dir is the offset from the RIGHT_LATCH half to
// the LEFT_LATCH half — the pair's left as seen from the front (the
// placer's left); the left half fills the merged UI's top rows.
static inline glm::ivec3 chest_pair_left_dir( unsigned char rotation ) {
    static const glm::ivec3 dirs[ 4 ] = {
        glm::ivec3( -1, 0, 0 ), // 0: front +z, left half at -x
        glm::ivec3( 0, 0, -1 ), // 1: front -x, left half at -z
        glm::ivec3( 1, 0, 0 ),  // 2: front -z, left half at +x
        glm::ivec3( 0, 0, 1 ),  // 3: front +x, left half at +z
    };
    return dirs[ rotation & 3 ];
}

// Offset from a latch half to the cell its partner must occupy. Returns
// the zero vector for a non-half id.
static inline glm::ivec3 chest_partner_offset( BlockID id, unsigned char rotation ) {
    if ( id == DOUBLE_CHEST_LEFT_LATCH ) {
        return -chest_pair_left_dir( rotation );
    }
    if ( id == DOUBLE_CHEST_RIGHT_LATCH ) {
        return chest_pair_left_dir( rotation );
    }
    return glm::ivec3( 0 );
}

// The complementary half id (LAST_BLOCK_ID for a non-half id).
static inline BlockID chest_partner_id( BlockID id ) {
    if ( id == DOUBLE_CHEST_LEFT_LATCH ) {
        return DOUBLE_CHEST_RIGHT_LATCH;
    }
    if ( id == DOUBLE_CHEST_RIGHT_LATCH ) {
        return DOUBLE_CHEST_LEFT_LATCH;
    }
    return LAST_BLOCK_ID;
}

// Chest contents keyed by world block position (file + server form). The
// client-side Chunk instead keys by in-chunk block index.
typedef std::map<glm::ivec3, ChestContents, Compare_I_Vec3> ChestContentsMap;
