#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include <glm/glm.hpp>

#include "common/constants.hpp" // PLAYER_WIDTH / PLAYER_HEIGHT defaults
#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/net/net_sync.hpp"
#include "common/vehicle_types.hpp" // VehicleType / vehicle_type_def

class Chunk;
class ChunkLoader;
class MapStorage;
class World;
struct VehicleFileRecord;
struct RepGameState;
class VertexBufferLayout;
class Renderer;
class Shader;

// Tick-driven vehicle entities, modelled on ItemEntities/ArrowEntities: a
// headless CPU simulation (rep_tests run it without GL) with a render chain
// only wired up after init() (the render_inited pattern). A Vehicle's
// per-type constants come from vehicle_type_def( v.type ); the owned sim
// dispatches on the def's `physics` — RAIL runs the rail-following
// minecart sim in vehicle_rail.cpp (flat track, ascending shapes and
// curves, powered-rail boost/brake, slope accel, empty-vs-ridden drag,
// derailment with move_box collision until re-snap or rest).
// Overlapping player bodies shove an empty cart — along the rail while on
// track, in world space while off it (a grounded cart can be pushed back
// onto a rail it enters).
//
// Multiplayer: owner-simulated like items — the chunk owner's client
// simulates and streams ENTITY_STATE; everyone else holds a mirror that
// eases toward the streamed target and never runs physics, collisions,
// pushes or rail stepping. Rider session ids ride the record/state
// `count` field and are server-authoritative; a ridden vehicle's rider
// session simulates it. Local carts_* file IO stays off in multiplayer
// (the server persists its own sidecars).
class Vehicles {
  public:
    static constexpr size_t MAX_VEHICLES = 128;

    struct Vehicle {
        uint32_t id = 0;                       // stable, non-zero; 0 means "no vehicle"
        VehicleType type = VEHICLE_MINECART;
        glm::dvec3 pos = glm::dvec3( 0.0 );    // box bottom-centre
        glm::dvec3 prev_pos = glm::dvec3( 0.0 );
        glm::dvec3 vel = glm::dvec3( 0.0 );    // blocks/tick, world space
        float yaw = 0.0f, prev_yaw = 0.0f;     // degrees; forward = (sin yaw, 0, -cos yaw)
        float pitch = 0.0f, prev_pitch = 0.0f; // degrees, + = nose up
        bool on_rail = false;
        int hurt_ticks = 0;     // wobble countdown
        float damage = 0.0f;    // survival hit accumulator, decays
        bool rider = false;     // local player riding
        int persistent = 1;
        std::vector<entt::entity> parts; // render chain entities (render_inited only)
        // Multiplayer sync (net.id 0 / net.mirror false in single player):
        // mirrors skip physics entirely and ease toward net.pos. Pitch is
        // not streamed — derived per tick under the mirror (physics-def'd).
        NetSync net;
        // Rider session id (server-authoritative `count` field): nonzero
        // means that session simulates the vehicle, not the chunk owner.
        uint32_t rider_session = 0;
        float net_yaw = 0.0f;          // latest streamed yaw (mirrors)
        uint32_t net_flags = 0;        // latest streamed NET_VEHICLE_FLAG_* (edge detect)
        float net_sent_yaw = 0.0f;     // yaw streamed with the last ENTITY_STATE
        uint32_t net_sent_flags = 0;   // flags streamed with the last ENTITY_STATE
        // Render-local paddle phase for WATER-physics vehicles (never
        // streamed): advances with horizontal speed while ridden, and the
        // render pass turns it into the paddle bones' rowing anim_rot.
        float paddle_phase = 0.0f;
        // WATER-physics angular velocity, degrees/s (boat steering is a
        // damped turn-rate model; mirrors take the streamed yaw instead).
        float yaw_rate = 0.0f;
    };

    // Per-tick input from the game loop; player_feet/width/height drive
    // empty-vehicle pushing.
    struct VehicleTickInput {
        uint32_t rider_vehicle = 0;                 // vehicle id the player rides (0 = none)
        glm::dvec2 rider_push = glm::dvec2( 0.0 );  // world xz forward intent, unit-ish (0 = no input)
        // WATER-physics rider input: forward/backward paddle along the
        // boat's yaw (W/S) and A/D yaw rate request, each -1..1.
        double rider_forward = 0.0;
        double rider_turn = 0.0;
        glm::dvec3 player_feet = glm::dvec3( 0.0 ); // for pushing empty vehicles
        double player_width = PLAYER_WIDTH;
        double player_height = PLAYER_HEIGHT;
        bool player_no_clip = false;
        // Remote players' feet positions (multiplayer only): they shove
        // owned empty vehicles with the same overlap rule as the local player.
        std::vector<glm::dvec3> extra_pushers;
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance );
    void init_storage( MapStorage *s );

    // Spawns a `type` vehicle bottom-centred on `pos`. Returns its id
    // (never 0); 0 at the cap or in a multiplayer session before WELCOME
    // (the spawn can't be announced). A connected session mints the net
    // id here and sends ENTITY_SPAWN like an item drop.
    uint32_t spawn( VehicleType type, const glm::dvec3 &pos, float yaw, int persistent = 1, bool from_net = false );
    // Right-click with MINECART on a rail cell: centre of cell, on the
    // rail, yaw along the rail axis nearest player_yaw.
    uint32_t spawn_on_rail( World &world, const glm::ivec3 &rail_cell, float player_yaw );

    // Per-game-tick simulation. Frozen in capture mode, and per vehicle
    // while the chunk at its position is not CHUNK_LIVE.
    void tick( RepGameState &state, const VehicleTickInput &in );

    // Nearest vehicle the ray hits within max_dist, or -1; *out_dist takes
    // the hit distance (like MobManager::raycast).
    int raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const;
    // Nearest vehicle nearer than the block hit at `block_dist`, or -1 —
    // the vehicle version of MobManager::resolve_target. `block_dist` must
    // be the pick ray's hit distance on the block's real pick shape: a
    // vehicle can sit inside its support's cell (a minecart on a rail),
    // so the whole-cell entry would always beat it.
    int resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double block_dist, double *out_dist ) const;

    // Hit: creative removes outright; survival adds decaying damage so
    // three quick hits break. Returns true when the vehicle was removed;
    // *drop set true when the caller should drop the type's item.
    bool hurt( int index, bool survival, bool *drop );

    // One left-click on `index` — the whole mirror/owned branch in one
    // place (player.cpp and the mp-test `carthit` ctl): a mirror gets an
    // ENTITY_ACTION NET_ACTION_HIT sent to its owner and no local damage;
    // an owned vehicle takes the hit through hurt() with its drop + remove
    // handled centrally. Returns 0 (miss/no-op), 1 (applied locally) or
    // 2 (action sent to the owner).
    int hit_interact( RepGameState &state, int index, bool survival, const glm::dvec3 &attacker_pos );

    bool net_mirror_at( int index ) const { return index >= 0 && this->vehicles[ index ].net.mirror; }
    NetEntityId net_id_at( int index ) const { return index >= 0 ? this->vehicles[ index ].net.id : 0; }

    glm::dvec3 pos_at( int index ) const { return this->vehicles[ index ].pos; }
    uint32_t id_at( int index ) const { return this->vehicles[ index ].id; }
    int index_of( uint32_t id ) const;
    Vehicle *find( uint32_t id );

    // Persistence — mirrors ItemEntities: carts_x_y_z files beside
    // items_x_y_z (the on-disk name is unchanged), pending records
    // drained on chunk adopt, the same owned-chunk leave poll persisting
    // vehicles whose chunk left the active window, and persist_all from
    // World::cleanup. No reads/writes in capture mode, fly mode, block
    // world, multiplayer, or REPGAME_MOBS=0 / REPGAME_MOB_TEST runs.
    void adopt_chunk( Chunk &chunk );
    void persist_all( );

    // Multiplayer sync (NetEntities drives these; all no-ops single
    // player). Local carts_* file reads/writes stay off while mp_mode —
    // the server persists the authoritative records.
    void set_multiplayer_mode( bool mp_mode, NetEntities *net );
    bool multiplayer_mode( ) const { return this->mp_mode; }
    // Incoming record handlers (called by NetEntities).
    Vehicle *find_by_net_id( NetEntityId net_id );
    const Vehicle *find_by_net_id( NetEntityId net_id ) const;
    // Where a remote avatar's feet sit inside the vehicle body — shared
    // by MultiplayerAvatars::apply_vehicle_seats and the `fake_rider` cmd.
    static glm::dvec3 seat_pos( const Vehicle &v );
    // Whether this client simulates `v`: a ridden vehicle is simulated by
    // its rider's session, an empty one by its net.chunk's owner.
    bool sim_here( const Vehicle &v ) const;
    // ENTITY_SPAWN relay: decide the mirror flag ourselves (rider-aware).
    void net_apply_spawn( const NetEntityRecord &rec );
    // CHUNK_ROLE record / ENTITY_TRANSFER adopt: explicit mirror flag.
    void net_apply_spawn( const NetEntityRecord &rec, bool mirror );
    void net_apply_state( const NetEntityStateEntry &e );
    void net_apply_remove( NetEntityId net_id );
    // CHUNK_ROLE: drop the chunk (NONE) or re-sync it to the authoritative
    // record set (VIEWER -> mirrors, OWNER -> locally simulated).
    void net_apply_chunk_role( const glm::ivec3 &chunk, uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids );
    // ENTITY_TRANSFER adopt=0: become a mirror if our role still covers the
    // entity's chunk, else drop it.
    void net_release_entity( const NetEntityRecord &rec, bool still_viewed );
    // ENTITY_ACTION forwarded by the server to the chunk owner.
    void net_apply_action( RepGameState &state, const NetEntityActionPayload &p );
    // Owner-side outbound batch (dead-reckoned + drift/keyframe/chunk/
    // stale/flags-changed throttle, same as items).
    void collect_net_state( NetEntities &net, std::vector<NetEntityStateEntry> &out, long tick_number );

    // Mount/dismount (multiplayer only — SP calls repgame_vehicle_mount
    // directly). request_mount sends ENTITY_ACTION MOUNT and waits for the
    // server's COUNT state; when rider_session becomes our session the
    // pending mount completes through repgame_vehicle_mount.
    bool request_mount( RepGameState &state, int index );
    // Per-tick pending-mount bookkeeping (completion, refusal, 3 s
    // timeout) — called from tick().
    void tick_pending_mount( RepGameState &state );
    // After repgame_vehicle_dismount places the camera: stream an immediate
    // parked-position ENTITY_STATE then ENTITY_ACTION DISMOUNT, and keep
    // simulating until the server's COUNT=0 lands.
    void local_dismount( RepGameState &state, Vehicle *vehicle );
    // Connection dropped while riding: local dismount (no wire traffic).
    void net_disconnect( RepGameState &state );

    // Rebuilds the instance buffer for this frame. alpha interpolates
    // prev_pos/prev_yaw/prev_pitch.
    void update_render( float alpha, const ChunkLoader &loader );
    void draw( const Renderer &renderer, const Shader &mob_shader );

    size_t count( ) const { return this->vehicles.size( ); }
    std::vector<Vehicle> debug_vehicles( ) const { return this->vehicles; }
    void cleanup( );

    // rep_tests seams: bind the sync hub and mutate entities without a full
    // RepGameState tick.
    void test_bind_net( NetEntities *n ) { this->net = n; }
    std::vector<Vehicle> &test_vehicles( ) { return this->vehicles; }

  private:
    void remove_vehicle( size_t i );
    bool vehicle_chunk_live( World &world, const Vehicle &v ) const;
    void poll_owned( World &world );
    void drop_render_parts( Vehicle &v );
    uint32_t spawn_record( const NetEntityRecord &rec, bool mirror );
    // Send ENTITY_REMOVE for a synced entity we own (no-op otherwise).
    void send_net_remove( Vehicle &v, uint8_t reason );
    // hurt() + the shared remove-side effects: the type's item drop —
    // the one place a broken vehicle drops (local clicks and forwarded
    // ENTITY_ACTIONs alike, so MP and SP drop exactly once).
    bool hurt_local( RepGameState &state, int index, bool survival );

    MapStorage *storage = nullptr;
    // Bound to state.multiplayer.entities while multiplayer_mode — the sync
    // hub. net_id minting and sends route through it.
    NetEntities *net = nullptr;
    bool mp_mode = false;
    int poll_count = 0;
    // Pending server-arbitrated mount: the vehicle we sent MOUNT for and
    // the tick we sent it (3 s timeout).
    NetEntityId pending_mount_id = 0;
    long pending_mount_tick = 0;
    // Apply a record's rider (`count`) field to a vehicle.
    void apply_rider( RepGameState *state, Vehicle &v, int32_t count );

    std::vector<Vehicle> vehicles;
    uint32_t next_id = 1;

    bool render_inited = false;
    // One MobPartInstance per model cube, drawn with the mob shader +
    // mobs texture (vehicles ride the mob model pipeline).
    RenderChain<ParticleVertex, MobPartInstance> render_chain;

    // A chunk this manager took ownership of via adopt_chunk: its vehicles
    // file was loaded (or known absent) and its vehicles persist back to
    // disk when the chunk leaves the active window or the world exits.
    struct OwnedVehicleChunk {
        glm::ivec3 pos;
        int file_present;
    };
    std::vector<OwnedVehicleChunk> owned_chunks;
};

// Vehicle collision box from the type def (pos is bottom-centre).
inline glm::dvec3 vehicle_min( const VehicleTypeDef &def, const glm::dvec3 &pos ) {
    return pos - glm::dvec3( def.width / 2.0, 0.0, def.width / 2.0 );
}
inline glm::dvec3 vehicle_max( const VehicleTypeDef &def, const glm::dvec3 &pos ) {
    return pos + glm::dvec3( def.width / 2.0, def.height, def.width / 2.0 );
}
