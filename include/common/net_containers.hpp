#pragma once

// Client side of container sync — the CONTAINER_* wire family. Lives on
// Multiplayer as `state.multiplayer.containers`, mirroring NetEntities:
// senders below build the payloads, handle() is the sub-router that applies
// authoritative CONTENTS / SET_SLOT frames to World-owned container state.
// UI panels repaint by pulling from World each frame (repgame_*_sync_icons),
// so handle() never touches ui state — it parses, bounds-checks the kind,
// and stores.

#include <map>

#include <glm/glm.hpp>

#include "common/container.hpp"
#include "common/net/packet.hpp"

class Multiplayer;
struct RepGameState;

class NetContainers {
    Multiplayer *mp;

    // Last revision the server reported per container (one entry per
    // kind+pos). Debug/self-test visibility: the server only ever bumps
    // this, so a lower incoming revision would mean a desync — the apply
    // paths still take the frame (server is authoritative) but the value
    // is recorded either way.
    std::map<glm::ivec3, uint32_t, Compare_I_Vec3> revisions[ NET_CONTAINER_KIND_COUNT ];

  public:
    explicit NetContainers( Multiplayer *mp ) : mp( mp ) {
    }

    // Senders (no-ops while disconnected, like every Multiplayer send).
    void open( NetContainerKind kind, const glm::ivec3 &pos );
    void close( NetContainerKind kind, const glm::ivec3 &pos );
    void set_slot( NetContainerKind kind, const glm::ivec3 &pos, uint32_t slot_index, const InventorySlot &value );

    // Dispatch for CONTAINER_* message types; returns true when the type
    // was consumed. `r` reads the type-specific payload.
    bool handle( RepGameState &state, NetMsgType type, int32_t player_id, PacketReader &r );

    // Revision last seen for a container (0 = none recorded).
    uint32_t last_revision( NetContainerKind kind, const glm::ivec3 &pos ) const;
    void reset( );
};
