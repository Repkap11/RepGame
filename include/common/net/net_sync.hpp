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

    // Outbound owner-side throttle tracking: last-streamed pos/vel (the
    // receiver's dead-reckoning baseline) and the last keyframe send.
    bool sent = false;
    glm::dvec3 sent_pos = glm::dvec3( 0.0 );
    glm::dvec3 sent_vel = glm::dvec3( 0.0 );
    long sent_tick = 0;
    long keyframe_tick = -1;

    // Inbound receiver-side tracking: last streamed pos (the POS_DELTA
    // decode baseline). `pos` is the same value extrapolated forward for
    // dead reckoning between states; `recv_pos` stays the anchor.
    glm::dvec3 recv_pos = glm::dvec3( 0.0 );

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
    s.recv_pos = s.pos;
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

// Owner-side send throttle shared by the collect_net_state loops: since
// protocol 12 receivers dead-reckon pos forward with the streamed vel, an
// owner only emits when the sim diverges from what the receiver predicts
// (pos off by more than ~1/16 block, or vel changed), when aux fields
// changed, or on the once-a-second keyframe. Callers OR in their extra
// changed-fields (item count, mob yaw/health/flags).
static constexpr double NET_STATE_DRIFT_EPS = 0.0625;
static constexpr double NET_STATE_VEL_EPS = 1e-3;

// Forced-update backstop: a full absolute-pos state re-anchors the delta
// baseline and refreshes the server's believed chunk. Only entities that
// actually moved since the last send can have gone stale — a still entity
// re-sends nothing but its own record, so it never keyframes at all.
static constexpr long NET_STATE_KEYFRAME_TICKS = 10 * UPS_RATE;
static constexpr double NET_STATE_KEYFRAME_MOVE_EPS = 0.25;

inline bool netsync_keyframe_due( const NetSync &s, const glm::dvec3 &pos, const long tick ) {
    return tick - s.keyframe_tick >= NET_STATE_KEYFRAME_TICKS && //
           glm::length( pos - s.sent_pos ) > NET_STATE_KEYFRAME_MOVE_EPS;
}

// Would the server's believed chunk flip if it saw this pos? Emitting on
// the flip is what keeps the belief fresh for silent movers — it's the
// targeted replacement for the old once-a-second keyframe, since a chunk
// assignment can only go stale when a boundary is crossed.
inline bool netsync_chunk_changed( const NetSync &s, const glm::dvec3 &pos ) {
    return net_entity_next_chunk( s.chunk, pos ) != s.chunk;
}

// Record-freshness cap, independent of prediction accuracy: CHUNK_ROLE
// grants and relays hand other clients the last-streamed pos, so a
// perfectly dead-reckoned silent walker must still check in every couple
// of blocks or newcomers anchor it a whole leg behind.
static constexpr double NET_STATE_STALE_EPS = 2.0;
inline bool netsync_stale( const NetSync &s, const glm::dvec3 &pos ) {
    return s.sent && glm::length( pos - s.sent_pos ) > NET_STATE_STALE_EPS;
}

// Is the receiver's dead-reckoned pose (sent_pos + sent_vel * dt) far
// enough off the sim to need a new state? `vel` is the velocity the
// receiver predicts with — callers pass the effective sim vel (a stuck
// arrow predicts 0 even though it streams its entry direction for
// orientation).
inline bool netsync_drifted( const NetSync &s, const glm::dvec3 &pos, const glm::dvec3 &vel, const long tick ) {
    if ( !s.sent ) {
        return true;
    }
    const glm::dvec3 predicted = s.sent_pos + s.sent_vel * static_cast<double>( tick - s.sent_tick );
    return glm::length( pos - predicted ) > NET_STATE_DRIFT_EPS || //
           glm::length( vel - s.sent_vel ) > NET_STATE_VEL_EPS;
}

// The pos value the receiver actually holds after this send: POS_DELTA
// snaps to the 1/256-block wire grid (or falls back to POS_ABS when out of
// range) — predicting against the raw pos would drift against the wire.
inline glm::dvec3 netsync_decoded_pos( const NetSync &s, const glm::dvec3 &pos, const bool send_abs ) {
    if ( send_abs ) {
        return pos;
    }
    const glm::dvec3 d = pos - s.sent_pos;
    if ( glm::any( glm::greaterThan( glm::abs( d ), glm::dvec3( 127.5 ) ) ) ) {
        return pos; // encoder falls back to POS_ABS
    }
    return s.sent_pos + glm::round( d * NET_STATE_POS_SCALE ) / NET_STATE_POS_SCALE;
}

// The vel value the receiver decodes (i16 @1/4096 quantization, clamped to
// the +-8 wire range): dead reckoning must predict with the wire value,
// not the raw sim vel.
inline glm::dvec3 netsync_wire_vel( const glm::dvec3 &vel ) {
    return glm::clamp( glm::round( vel * NET_STATE_VEL_SCALE ), -32767.0, 32767.0 ) / NET_STATE_VEL_SCALE;
}

// After an ENTITY_STATE entry is pushed: record what the receiver holds
// (decoded pos, wire vel) as the dead-reckoning baseline and advance the
// server-belief chunk (the server applies the same hysteresis to this pos).
inline void netsync_mark_sent( NetSync &s, const glm::dvec3 &pos, const glm::dvec3 &vel, const long tick, const bool keyframe ) {
    s.sent = true;
    s.sent_pos = pos;
    s.sent_vel = vel;
    s.recv_pos = pos;
    s.sent_tick = tick;
    s.chunk = net_entity_next_chunk( s.chunk, pos );
    if ( keyframe ) {
        s.keyframe_tick = tick;
    }
}
