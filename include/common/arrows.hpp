#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/item.hpp"
#include "common/item_entities.hpp" // ItemSpriteInstance
#include "common/net/packet.hpp"
#include "common/net/net_sync.hpp"

class ChunkLoader;
class World;
class NetEntities;
struct RepGameState;

// Arrow record/state flag bits (NetEntityRecord::flags /
// NetEntityStateEntry::flags).
#define NET_ARROW_FLAG_STUCK ( 1u << 0 )            // parked in a block
#define NET_ARROW_FLAG_ATTACKER_SURVIVAL ( 1u << 1 ) // shooter's mode at fire

// Tick-driven arrow projectiles, modelled on ItemEntities/BlockBodies: a
// headless CPU simulation (rep_tests run it without GL) with a render chain
// only wired up after init() (the render_inited pattern). An arrow is a
// point-sized body: it flies a ballistic arc, sticks into the first
// colliding block it reaches (falling free again when that block breaks),
// damages the first mob hitbox it crosses, and despawns after 5 minutes.
// Stuck arrows can be walked over to collect them back as ARROW items.
//
// Multiplayer: "causer simulates" like BlockBodies — the shooter's client
// runs the sim and streams ENTITY_STATE; other clients hold mirrors that
// only ease toward the last streamed pose. Arrows are never persisted to
// disk or written to item/mob sidecar files.
class ArrowEntities {
  public:
    static constexpr size_t MAX_ARROWS = 256;
    // Despawn at 5 minutes of un-frozen ticks, same budget as dropped items.
    static constexpr int ARROW_DESPAWN_TICKS = 5 * 60 * UPS_RATE;
    // Walk-over pickup cooldown after landing — the same delay a
    // player-thrown item spawns with (ItemEntities).
    static constexpr int ARROW_PICKUP_DELAY_TICKS = 2 * UPS_RATE;
    // Full-draw release speed in blocks/tick (~60 blocks/s at UPS_RATE).
    static constexpr double ARROW_SPEED = 0.42;
    // Flat hit damage in survival; creative firing uses the one-shot
    // 1000.0f the melee path uses.
    static constexpr float ARROW_DAMAGE = 9.0f;

    struct Arrow {
        uint32_t id = 0; // stable, non-zero; 0 means "no arrow"
        glm::dvec3 pos = glm::dvec3( 0.0 );      // arrow center
        glm::dvec3 prev_pos = glm::dvec3( 0.0 );
        glm::dvec3 vel = glm::dvec3( 0.0 );      // blocks/tick; while stuck, the entry direction
        glm::dvec3 orient = glm::dvec3( 0.0, 1.0, 0.0 ); // last nonzero flight direction (render)
        float damage = ARROW_DAMAGE;
        int attacker_survival = 1; // shooter's mode at fire: gates kill drops
        int stuck = 0;             // parked in a block: no integration
        glm::ivec3 stuck_cell = glm::ivec3( 0 ); // cell the embedded tip rests in
        int age_ticks = 0;
        int pickup_delay = 0; // stuck-tick countdown before walk-over pickup
        entt::entity entity = entt::null; // render chain entity (render_inited only)
        // Multiplayer sync (net.id 0 / net.mirror false in single player):
        // mirrors ease toward net.pos and render net.vel as the orientation.
        NetSync net;
    };

    // One arrow-mob collision for the caller to apply (hit feedback +
    // damage/drops, or ENTITY_ACTION for remote-owned mobs). mob_index is
    // valid until the next mobs.tick.
    struct ArrowMobHit {
        int mob_index;
        glm::dvec3 pos; // impact point (hurt() attacker_pos -> knockback dir)
        float damage;
        int attacker_survival;
    };
    struct ArrowTickOutput {
        std::vector<ArrowMobHit> mob_hits;
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_item_instance );

    // Per-game-tick simulation. Frozen in capture mode, and per arrow while
    // the chunk at its center is not CHUNK_LIVE. Mob hits are reported via
    // `out` for the caller to apply.
    void tick( RepGameState &state, ArrowTickOutput *out );

    // Spawns an arrow centered on `pos` flying at `vel` (blocks/tick).
    // Returns its id (never 0): at the cap the oldest arrow is evicted.
    uint32_t spawn( const glm::dvec3 &pos, const glm::dvec3 &vel, float damage, int attacker_survival, bool from_net = false );

    // Rebuilds the instance buffer for this frame. alpha interpolates
    // prev_pos -> pos; camera_pos rolls each sprite's plane toward the eye.
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &item_sprite_shader );

    size_t count( ) const { return this->arrows.size( ); }
    void cleanup( );
    // Test/debug: snapshot of every live arrow.
    std::vector<Arrow> debug_arrows( ) const { return this->arrows; }

    // ---- Multiplayer sync ("causer simulates"; NetEntities drives these —
    // all no-ops in single player). Same hook set as BlockBodies.
    void set_multiplayer_mode( bool mp_mode, NetEntities *net );
    bool multiplayer_mode( ) const { return this->mp_mode; }
    Arrow *find_by_net_id( NetEntityId net_id );
    void net_apply_spawn( const NetEntityRecord &rec, bool mirror );
    void net_apply_state( const NetEntityStateEntry &e );
    void net_apply_remove( NetEntityId net_id );
    // ENTITY_TRANSFER adopt=1 (sim handover after the causer disconnected):
    // become the simulating client.
    void net_apply_transfer( const NetEntityRecord &rec );
    // CHUNK_ROLE: NONE drops mirror arrows of that chunk; VIEWER/OWNER
    // resync mirrors against the records (owned arrows untouched).
    void net_apply_chunk_role( const glm::ivec3 &chunk, uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids );
    // Multiplayer dropped: mirror arrows would freeze mid-flight — remove them.
    void net_disconnect( );
    // ENTITY_STATE entries for arrows we simulate (ownership = !net.mirror).
    void collect_net_state( std::vector<NetEntityStateEntry> &out, long tick_number );

    // rep_tests seam: mutate entities without a full RepGameState tick.
    std::vector<Arrow> &test_arrows( ) { return this->arrows; }

  private:
    void remove_arrow( size_t i );
    bool arrow_chunk_live( World &world, const Arrow &a ) const;
    // Multiplayer: mint a net id for a just-spawned arrow and announce it
    // with ENTITY_SPAWN (no-op unbound / net.id already set).
    void announce_spawn( Arrow &a );
    void send_net_remove( Arrow &a, uint8_t reason );
    // Spawn one arrow from a wire record (mirror or freshly adopted).
    bool spawn_record( const NetEntityRecord &rec, bool mirror );

    NetEntities *net = nullptr;
    bool mp_mode = false;

    std::vector<Arrow> arrows;
    uint32_t next_id = 1;

    bool render_inited = false;
    // Arrows draw as the arrow icon's extruded sprite (the same 1536-quad
    // mesh dropped flat items share, exported by item_entities.cpp),
    // velocity-aligned and rolled toward the camera.
    RenderChain<ParticleVertex, ItemSpriteInstance> render_chain;
};
