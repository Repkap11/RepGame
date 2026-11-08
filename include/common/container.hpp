#pragma once

// Generic container model shared by the wire protocol (packet.cpp), the
// client sync hub (net_containers.cpp) and the server hub
// (server_containers.cpp). Every synced container kind is described by one
// NetContainerKindDef row — adding a barrel/hopper/etc. is a new row plus
// World/chunk storage for its contents, not another copy of the
// open/close/subscribe/tick machinery.
//
// This header is intentionally low-level (no renderer/GL types, no World):
// the server build links it without the client world code.

#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "common/block_definitions.hpp" // InventorySlot, BlockID
#include "common/chest.hpp"             // Compare_I_Vec3, CHEST_SLOTS, block_is_chest
#include "common/furnace.hpp"           // FURNACE_INVENTORY_SLOTS, block_is_furnace

class MapStorage;

// Container kinds carried by the CONTAINER_* message family. Values are
// stable on the wire; append only.
enum class NetContainerKind : uint8_t {
    CHEST = 0,
    FURNACE = 1,
};
static constexpr int NET_CONTAINER_KIND_COUNT = 2;

// Bounds for the variable-length sections of CONTAINER_CONTENTS (validated
// before allocation in the deserializer).
static constexpr uint8_t NET_CONTAINER_MAX_SLOTS = 64;
static constexpr uint8_t NET_CONTAINER_MAX_AUX = 4;

// Interchange blob for one container's contents: the slot array plus the
// kind's aux ints. Sizes always equal def->slots / def->aux for the owning
// kind; the wire and the server cache carry this shape, the client's World
// keeps typed per-kind views (ChestContents/FurnaceContents) converted at
// the boundary.
//   CHEST   aux: (none)
//   FURNACE aux: [0]=cook_progress ticks, [1]=burn_ticks remaining
struct ContainerContents {
    std::vector<InventorySlot> slots;
    std::vector<int32_t> aux;
};

// Container contents keyed by world block position (server cache + the
// load/persist hooks below).
typedef std::map<glm::ivec3, ContainerContents, Compare_I_Vec3> ContainerContentsMap;

// Per-kind description: wire arity (slots + aux counts), the block
// predicate, chunk-sidecar persistence, and the optional authoritative
// behaviour (server ticking / lit-block swap). The table lives in
// container.cpp; null/0 members mean "no such behaviour".
struct NetContainerKindDef {
    NetContainerKind kind;
    const char *name; // logs/debug: "chest" / "furnace"
    uint8_t slots;    // slots in CONTAINER_CONTENTS; SET_SLOT index bound
    uint8_t aux;      // trailing i32 fields in CONTAINER_CONTENTS
    bool ( *block_is )( BlockID id );
    // Chunk sidecar IO through the existing per-kind files (formats
    // unchanged). load merges file contents into `out`.
    void ( *load )( MapStorage &storage, const glm::ivec3 &chunk_pos, ContainerContentsMap &out );
    void ( *persist )( MapStorage &storage, const glm::ivec3 &chunk_pos, const ContainerContentsMap &contents );
    // Server-authoritative ticking, or nullptr for unticked kinds.
    // `active` reports whether this container could still change on its
    // own (gates the UPS_RATE wake). `tick` advances one tick; *changed
    // reports a contents/progress mutation (dirty + snapshot broadcast),
    // *lit_changed reports a burn>0 edge so the server can swap
    // unlit_block<->lit_block (the `lit` predicate reads the post-tick
    // state to pick which).
    bool ( *active )( const ContainerContents &contents );
    bool ( *tick )( ContainerContents &contents, bool *lit_changed, bool *changed );
    bool ( *lit )( const ContainerContents &contents );
    // Lit/unlit block pair for ticking kinds (LAST_BLOCK_ID = no swap).
    BlockID unlit_block;
    BlockID lit_block;
};

// Kind table lookup by wire value (nullptr for out-of-range), and by block
// id (nullptr when the block is no container kind).
const NetContainerKindDef *net_container_kind_def( uint8_t kind );
const NetContainerKindDef *net_container_kind_of_block( BlockID id );

// Normalize a slot write for storage: invalid ids and non-positive
// quantities become the empty slot, over-max quantities clamp. Used by the
// server's SET_SLOT path (the wire deserializer already bounds the index).
InventorySlot container_clean_slot( InventorySlot slot );

// Conversions between the blob and the typed per-kind views. Blobs are
// always exact-arity (def->slots/def->aux); from_blob pads/truncates a
// short/foreign blob defensively.
ContainerContents container_to_blob( const ChestContents &c );
ContainerContents container_to_blob( const FurnaceContents &f );
void container_from_blob( const ContainerContents &c, ChestContents &out );
void container_from_blob( const ContainerContents &c, FurnaceContents &out );
