#pragma once

// Shared replication state for simulated world entities (dropped items,
// mobs, block bodies) — the client side of "server arbitrates, owner client
// simulates". Every syncable entity embeds one of these; ownership is
// explicit: owners (!mirror) run physics/AI and stream ENTITY_STATE, mirrors
// never simulate — they render/update purely from the replicated pos/vel
// target, so per-entity visual fields (age, spin, gait, fuse blink) derive
// from the same values on every client and can't drift.

#include <vector>

#include <glm/glm.hpp>

#include "common/net/packet.hpp"
#include "common/net_entities.hpp"

struct NetSync {
    NetEntityId id = 0;                 // server-assigned identity; 0 = local-only
    bool mirror = false;                // remote-owned: no local simulation
    glm::ivec3 chunk = glm::ivec3( 0 ); // chunk the server believes it's in (hysteresis-tracked)
    glm::dvec3 pos = glm::dvec3( 0.0 ); // mirror interpolation target (entity-anchor semantics per type)
    glm::dvec3 vel = glm::dvec3( 0.0 );

    // Outbound owner-side throttle tracking: last-streamed pos/vel and the
    // last keyframe send (a forced update every UPS_RATE ticks).
    bool sent = false;
    glm::dvec3 sent_pos = glm::dvec3( 0.0 );
    glm::dvec3 sent_vel = glm::dvec3( 0.0 );
    long keyframe_tick = -1;

    bool synced( ) const { return id != 0; }
    bool owned( ) const { return !mirror; }
};

// Linear find over an entity vector whose members embed `NetSync net`.
template <typename T>
inline T *netsync_find( std::vector<T> &entities, const NetEntityId id ) {
    if ( id == 0 ) {
        return nullptr;
    }
    for ( T &e : entities ) {
        if ( e.net.id == id ) {
            return &e;
        }
    }
    return nullptr;
}

// Shared tail of spawn_record: bind the wire record to the entity's sync
// state. mirror=true means another client simulates it.
inline void netsync_apply_record( NetSync &s, const NetEntityRecord &rec, const bool mirror ) {
    s.id = rec.id;
    s.mirror = mirror;
    s.pos = glm::dvec3( rec.x, rec.y, rec.z );
    s.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
    s.chunk = net_entity_chunk_of( s.pos );
}

// Advance the server-belief chunk from a streamed pos (same boundary
// hysteresis the server applies to every ENTITY_STATE).
inline void netsync_track_chunk( NetSync &s, const glm::dvec3 &pos ) {
    s.chunk = net_entity_next_chunk( s.chunk, pos );
}

// ENTITY_REMOVE for a synced entity in the chunk-owner-simulates model
// (items, mobs): never announce removals for chunks we don't own.
inline void netsync_send_remove_owned( NetEntities *net, const NetSync &s, const uint8_t reason ) {
    if ( net == nullptr || s.id == 0 ) {
        return;
    }
    if ( net->owns_chunk( s.chunk ) ) {
        net->send_remove( s.id, reason );
    }
}

// ENTITY_REMOVE for a causer-simulates entity (bodies): whoever simulates
// streams it, regardless of which chunk it currently sits in.
inline void netsync_send_remove( NetEntities *net, const NetSync &s, const uint8_t reason ) {
    if ( net == nullptr || s.id == 0 || s.mirror ) {
        return;
    }
    net->send_remove( s.id, reason );
}

// Owner-side send throttle shared by all three collect_net_state loops: the
// 1e-4 pos/vel drift tolerance and the once-a-second keyframe. Callers OR in
// their extra changed-fields (item count, mob yaw/health/flags).
inline bool netsync_keyframe_due( const NetSync &s, const long tick ) {
    return tick - s.keyframe_tick >= UPS_RATE;
}

inline bool netsync_moved( const NetSync &s, const glm::dvec3 &pos, const glm::dvec3 &vel ) {
    return glm::length( pos - s.sent_pos ) > 1e-4 || glm::length( vel - s.sent_vel ) > 1e-4;
}

// After an ENTITY_STATE entry is pushed: record what was streamed and
// advance the server-belief chunk (the server applies the same hysteresis
// to this pos).
inline void netsync_mark_sent( NetSync &s, const glm::dvec3 &pos, const glm::dvec3 &vel, const long tick, const bool keyframe ) {
    s.sent = true;
    s.sent_pos = pos;
    s.sent_vel = vel;
    s.chunk = net_entity_next_chunk( s.chunk, pos );
    if ( keyframe ) {
        s.keyframe_tick = tick;
    }
}
