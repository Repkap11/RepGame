#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include <glm/glm.hpp>

#include "common/constants.hpp" // PLAYER_WIDTH / PLAYER_HEIGHT defaults
#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/net/net_sync.hpp"

class Chunk;
class ChunkLoader;
class MapStorage;
class World;
struct CartFileRecord;
struct RepGameState;
class VertexBufferLayout;
class Renderer;
class Shader;

// Tick-driven minecart entities, modelled on ItemEntities/ArrowEntities: a
// headless CPU simulation (rep_tests run it without GL) with a render chain
// only wired up after init() (the render_inited pattern). A cart is a box
// that follows rail cells (see rail.hpp) — flat track, ascending shapes
// and curves — with MC-style physics: powered rails boost, dead
// ones brake, slopes accelerate downhill, empty carts lose more speed than
// ridden ones. Derailed carts fall through the world with move_box
// collision until they re-snap onto a rail or rest on the ground.
//
// Multiplayer phase 1 (empty carts): owner-simulated like items — the
// chunk owner's client simulates and streams ENTITY_STATE; everyone else
// holds a mirror that eases toward the streamed target and never runs
// physics, collisions, pushes or rail stepping. Rider session ids ride the
// record/state `count` field and are server-authoritative (phase 2 uses
// them; phase 1 always streams 0). Local carts_* file IO stays off in
// multiplayer (the server persists its own sidecars).
class Minecarts {
  public:
    static constexpr size_t MAX_CARTS = 128;
    static constexpr double CART_WIDTH = 0.98;
    static constexpr double CART_HEIGHT = 0.7;

    struct Cart {
        uint32_t id = 0;                       // stable, non-zero; 0 means "no cart"
        glm::dvec3 pos = glm::dvec3( 0.0 );    // box bottom-centre
        glm::dvec3 prev_pos = glm::dvec3( 0.0 );
        glm::dvec3 vel = glm::dvec3( 0.0 );    // blocks/tick, world space
        float yaw = 0.0f, prev_yaw = 0.0f;     // degrees; forward = (sin yaw, 0, -cos yaw)
        float pitch = 0.0f, prev_pitch = 0.0f; // degrees, + = nose up
        bool on_rail = false;
        int hurt_ticks = 0;     // wobble countdown
        float damage = 0.0f;    // survival hit accumulator, decays
        bool rider = false;     // local player riding (Phase C wires it)
        int persistent = 1;
        std::vector<entt::entity> parts; // render chain entities (render_inited only)
        // Multiplayer sync (net.id 0 / net.mirror false in single player):
        // mirrors skip physics entirely and ease toward net.pos. Pitch is
        // not streamed — derived per tick from the rail under the mirror.
        NetSync net;
        // Rider session id (server-authoritative `count` field): nonzero
        // means that session simulates the cart, not the chunk owner.
        uint32_t rider_session = 0;
        float net_yaw = 0.0f;          // latest streamed yaw (mirrors)
        uint32_t net_flags = 0;        // latest streamed NET_CART_FLAG_* (edge detect)
        float net_sent_yaw = 0.0f;     // yaw streamed with the last ENTITY_STATE
        uint32_t net_sent_flags = 0;   // flags streamed with the last ENTITY_STATE
    };

    // Per-tick input from the game loop. The rider fields stay zero until
    // Phase C; player_feet/width/height drive empty-cart pushing.
    struct CartTickInput {
        uint32_t rider_cart = 0;                    // cart id the player rides (0 = none)
        glm::dvec2 rider_push = glm::dvec2( 0.0 );  // world xz forward intent, unit-ish (0 = no input)
        glm::dvec3 player_feet = glm::dvec3( 0.0 ); // for pushing empty carts
        double player_width = PLAYER_WIDTH;
        double player_height = PLAYER_HEIGHT;
        bool player_no_clip = false;
        // Remote players' feet positions (multiplayer only): they shove
        // owned empty carts with the same overlap rule as the local player.
        std::vector<glm::dvec3> extra_pushers;
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance );
    void init_storage( MapStorage *s );

    // Spawns a cart bottom-centred on `pos`, snapping onto a rail there.
    // Returns its id (never 0); 0 at the cap or in a multiplayer session
    // before WELCOME (the spawn can't be announced). A connected session
    // mints the net id here and sends ENTITY_SPAWN like an item drop.
    uint32_t spawn( const glm::dvec3 &pos, float yaw, int persistent = 1, bool from_net = false );
    // Right-click with MINECART on a rail cell: centre of cell, on the
    // rail, yaw along the rail axis nearest player_yaw.
    uint32_t spawn_on_rail( World &world, const glm::ivec3 &rail_cell, float player_yaw );

    // Per-game-tick simulation. Frozen in capture mode, and per cart while
    // the chunk at its position is not CHUNK_LIVE.
    void tick( RepGameState &state, const CartTickInput &in );

    // Nearest cart the ray hits within max_dist, or -1; *out_dist takes
    // the hit distance (like MobManager::raycast).
    int raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const;
    // Nearest cart nearer than the block hit at `block_dist`, or -1 — the
    // cart version of MobManager::resolve_target. `block_dist` must be the
    // pick ray's hit distance on the block's real pick shape: a cart sits
    // inside its rail's cell, so the whole-cell entry would always beat it.
    int resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double block_dist, double *out_dist ) const;

    // Hit: creative removes outright; survival adds decaying damage so
    // three quick hits break. Returns true when the cart was removed;
    // *drop set true when the caller should drop a MINECART item.
    bool hurt( int index, bool survival, bool *drop );

    // One left-click on `index` — the whole mirror/owned branch in one
    // place (player.cpp and the mp-test `carthit` ctl): a mirror gets an
    // ENTITY_ACTION NET_ACTION_HIT sent to its owner and no local damage;
    // an owned cart takes the hit through hurt() with its drop + remove
    // handled centrally. Returns 0 (miss/no-op), 1 (applied locally) or
    // 2 (action sent to the owner).
    int hit_interact( RepGameState &state, int index, bool survival, const glm::dvec3 &attacker_pos );

    bool net_mirror_at( int index ) const { return index >= 0 && this->carts[ index ].net.mirror; }
    NetEntityId net_id_at( int index ) const { return index >= 0 ? this->carts[ index ].net.id : 0; }

    glm::dvec3 pos_at( int index ) const { return this->carts[ index ].pos; }
    uint32_t id_at( int index ) const { return this->carts[ index ].id; }
    int index_of( uint32_t id ) const;
    Cart *find( uint32_t id );

    // Persistence — mirrors ItemEntities: carts_x_y_z files beside
    // items_x_y_z, pending records drained on chunk adopt, the same
    // owned-chunk leave poll persisting carts whose chunk left the active
    // window, and persist_all from World::cleanup. No reads/writes in
    // capture mode, fly mode, block world, multiplayer, or
    // REPGAME_MOBS=0 / REPGAME_MOB_TEST runs.
    void adopt_chunk( Chunk &chunk );
    void persist_all( );

    // Multiplayer sync (NetEntities drives these; all no-ops single
    // player). Local carts_* file reads/writes stay off while mp_mode —
    // the server persists the authoritative records.
    void set_multiplayer_mode( bool mp_mode, NetEntities *net );
    bool multiplayer_mode( ) const { return this->mp_mode; }
    // Incoming record handlers (called by NetEntities).
    Cart *find_by_net_id( NetEntityId net_id );
    const Cart *find_by_net_id( NetEntityId net_id ) const;
    // Where a remote avatar's feet sit inside the cart body — shared by
    // MultiplayerAvatars::apply_cart_seats and the script `fake_rider` cmd.
    static glm::dvec3 seat_pos( const Cart &c );
    // Whether this client simulates `c`: a ridden cart is simulated by its
    // rider's session, an empty one by its net.chunk's owner.
    bool sim_here( const Cart &c ) const;
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

    // Mount/dismount (multiplayer only — SP calls repgame_cart_mount
    // directly). request_mount sends ENTITY_ACTION MOUNT and waits for the
    // server's COUNT state; when rider_session becomes our session the
    // pending mount completes through repgame_cart_mount.
    bool request_mount( RepGameState &state, int index );
    // Per-tick pending-mount bookkeeping (completion, refusal, 3 s
    // timeout) — called from tick().
    void tick_pending_mount( RepGameState &state );
    // After repgame_cart_dismount places the camera: stream an immediate
    // parked-position ENTITY_STATE then ENTITY_ACTION DISMOUNT, and keep
    // simulating until the server's COUNT=0 lands.
    void local_dismount( RepGameState &state, Cart *cart );
    // Connection dropped while riding: local dismount (no wire traffic).
    void net_disconnect( RepGameState &state );

    // Rebuilds the instance buffer for this frame. alpha interpolates
    // prev_pos/prev_yaw/prev_pitch.
    void update_render( float alpha, const ChunkLoader &loader );
    void draw( const Renderer &renderer, const Shader &mob_shader );

    size_t count( ) const { return this->carts.size( ); }
    std::vector<Cart> debug_carts( ) const { return this->carts; }
    void cleanup( );

    // rep_tests seams: bind the sync hub and mutate entities without a full
    // RepGameState tick.
    void test_bind_net( NetEntities *n ) { this->net = n; }
    std::vector<Cart> &test_carts( ) { return this->carts; }

  private:
    void remove_cart( size_t i );
    bool cart_chunk_live( World &world, const Cart &c ) const;
    void poll_owned( World &world );
    void drop_render_parts( Cart &c );
    uint32_t spawn_record( const NetEntityRecord &rec, bool mirror );
    // Send ENTITY_REMOVE for a synced entity we own (no-op otherwise).
    void send_net_remove( Cart &c, uint8_t reason );
    // hurt() + the shared remove-side effects: the MINECART item drop —
    // the one place a broken cart drops (local clicks and forwarded
    // ENTITY_ACTIONs alike, so MP and SP drop exactly once).
    bool hurt_local( RepGameState &state, int index, bool survival );

    MapStorage *storage = nullptr;
    // Bound to state.multiplayer.entities while multiplayer_mode — the sync
    // hub. net_id minting and sends route through it.
    NetEntities *net = nullptr;
    bool mp_mode = false;
    int poll_count = 0;
    // Pending server-arbitrated mount: the cart we sent MOUNT for and the
    // tick we sent it (3 s timeout).
    NetEntityId pending_mount_id = 0;
    long pending_mount_tick = 0;
    // Apply a record's rider (`count`) field to a cart.
    void apply_rider( RepGameState *state, Cart &c, int32_t count );

    std::vector<Cart> carts;
    uint32_t next_id = 1;

    bool render_inited = false;
    // One MobPartInstance per model cube, drawn with the mob shader +
    // mobs texture (the minecart model rides the mob model pipeline).
    RenderChain<ParticleVertex, MobPartInstance> render_chain;

    // A chunk this manager took ownership of via adopt_chunk: its carts
    // file was loaded (or known absent) and its carts persist back to
    // disk when the chunk leaves the active window or the world exits.
    struct OwnedCartChunk {
        glm::ivec3 pos;
        int file_present;
    };
    std::vector<OwnedCartChunk> owned_chunks;
};
