#pragma once

// Client side of entity sync ("server arbitrates, owner client simulates").
// NetEntities lives on Multiplayer as `state.multiplayer.entities`: it
// tracks the session id, the per-chunk OWNER/VIEWER roles the server grants
// via CHUNK_ROLE, sends VIEW updates, and streams ENTITY_STATE for entities
// in chunks we own. Incoming records are applied to the client-local sims
// (ItemEntities for phase 1; mobs/bodies later).

#include <map>
#include <set>
#include <vector>

#include <glm.hpp>

#include "common/chest.hpp" // Compare_I_Vec3
#include "common/net/packet.hpp"

class Multiplayer;
struct RepGameState;

class NetEntities {
    Multiplayer *mp;

    uint32_t session_id = 0;
    uint32_t entity_counter = 1;
    bool welcomed = false;

    // Granted role per chunk (NONE entries are not stored).
    std::map<glm::ivec3, uint8_t, Compare_I_Vec3> roles;

    // CHUNK_ROLE grants for chunks that aren't CHUNK_LIVE locally yet;
    // applied from tick() once the chunk finalizes.
    struct PendingRole {
        glm::ivec3 chunk;
        uint8_t role;
        uint8_t populated;
        std::vector<NetEntityRecord> records;
        // Ids that entered this chunk (spawn relays, transfers, our own
        // spawns) after the grant was received: they are legitimately absent
        // from the stale record snapshot, so apply-time resync must keep them.
        std::set<NetEntityId> late_ids;
    };
    std::vector<PendingRole> pending_roles;

    // Ids removed this session (ENTITY_REMOVE). Filters stale record lists:
    // an entity removed while its chunk's grant sat pending must not be
    // resurrected when the grant applies. Ids are never reused.
    std::set<NetEntityId> gone_ids;

    // Last VIEW we sent, to resend only on change.
    bool view_sent = false;
    glm::ivec3 view_center = glm::ivec3( 0 );
    int view_rx = -1;
    int view_ry = -1;

    void apply_chunk_role( RepGameState &state, const PendingRole &grant );
    PendingRole *pending_for( const glm::ivec3 &chunk );
    bool chunk_live( RepGameState &state, const glm::ivec3 &chunk ) const;
    void send_view( int cx, int cy, int cz, int rx, int ry );

  public:
    explicit NetEntities( Multiplayer *mp ) : mp( mp ) {
    }

    // Dispatch for entity-protocol message types; returns true when the type
    // was consumed. `r` reads the type-specific payload.
    bool handle( RepGameState &state, NetMsgType type, int32_t player_id, PacketReader &r );

    // Per game tick: apply pending grants, send VIEW on change, and stream
    // ENTITY_STATE for owned entities.
    void tick( RepGameState &state );

    // Connection dropped: forget the session and all roles. Local entities
    // stay as they are (no persistence either way in multiplayer mode).
    void reset( );

    bool connected( ) const { return this->welcomed; }
    uint32_t session( ) const { return this->session_id; }
    // Granted non-NONE roles (mp_test `ready` detection + dump summary).
    size_t role_count( ) const { return this->roles.size( ); }
    void role_counts( int *owners, int *viewers ) const {
        *owners = *viewers = 0;
        for ( const auto &pair : this->roles ) {
            if ( pair.second == NET_ROLE_OWNER ) {
                ( *owners )++;
            } else if ( pair.second == NET_ROLE_VIEWER ) {
                ( *viewers )++;
            }
        }
    }

    // Mint a client-side entity id: (session_id << 32) | counter. Returns 0
    // before WELCOME — those entities stay local-only.
    NetEntityId mint_id( );

    int role_of( const glm::ivec3 &chunk ) const;
    bool owns_chunk( const glm::ivec3 &chunk ) const;
    bool views_chunk( const glm::ivec3 &chunk ) const;

    // Send helpers used by the entity sims.
    void send_spawn( const NetEntityRecord &rec );
    void send_remove( NetEntityId id, uint8_t reason );
    void send_pickup_request( NetEntityId id, int32_t room );
    void send_entity_action( const NetEntityActionPayload &p );
    void send_explosion( double x, double y, double z, float radius );
    void send_chunk_populated( const glm::ivec3 &chunk );
    void send_player_hit( int32_t target_player_id, const glm::dvec3 &from, float damage );

    // rep_tests seam: grant a role without a CHUNK_ROLE frame.
    void test_set_role( const glm::ivec3 &chunk, int role ) {
        this->roles[ chunk ] = static_cast<uint8_t>( role );
    }
};
