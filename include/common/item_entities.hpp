#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/block.hpp"
#include "common/item.hpp"
#include "common/net/packet.hpp"
#include "common/net/net_sync.hpp"
#include "common/utils/collision.hpp"

class ChunkLoader;
class World;
class Chunk;
class MapStorage;
class NetEntities;
struct RepGameState;

// Per-instance data for the extruded-sprite item draw: one shared 1536-quad
// mesh (item_entities.cpp) is instanced per item, sampling a single atlas
// layer in item_sprite_vertex.glsl. Every quad is pinned to a single texel's
// center uv so the per-texel alpha test is all-or-nothing per quad — MSAA
// pixel-center shading then can't drop covered samples at texel boundaries.
// The transform is rebuilt each frame by ItemEntities::update_render (spin +
// bob on the CPU).
typedef struct {
    unsigned int tex_layer; // atlas layer of the item's flat icon
    glm::mat4 transform;    // unit-square sprite -> world
    glm::vec2 light;        // flood-fill light at the item center: x=sky, y=block, [0,1]
} ItemSpriteInstance;

// Tick-driven dropped-item entities, modelled on BlockBodies: a headless CPU
// simulation (rep_tests run it without GL) with render chains only wired up
// after init() (the render_inited pattern). Items are 0.25 cubes that fall,
// rest, merge, magnet toward the player and despawn. Local to the client —
// multiplayer treats them like BlockBodies (server-authoritative loot still
// lands in the inventory directly).
class ItemEntities {
  public:
    // Cube edge length — a quarter block.
    static constexpr double ITEM_SIZE = 0.25;
    static constexpr size_t MAX_ITEM_ENTITIES = 512;
    // Despawn at 5 minutes of un-frozen ticks.
    static constexpr int ITEM_DESPAWN_TICKS = 5 * 60 * UPS_RATE;

    struct ItemEntity {
        uint32_t id = 0; // stable, non-zero; 0 means "no entity"
        ItemID item = LAST_BLOCK_ID;
        int count = 0;
        glm::dvec3 pos = glm::dvec3( 0.0 );      // box bottom-center
        glm::dvec3 prev_pos = glm::dvec3( 0.0 );
        glm::dvec3 vel = glm::dvec3( 0.0 );      // blocks/tick
        int age_ticks = 0;
        int pickup_delay = 0;
        int grounded = 0;
        int in_water = 0;
        int persistent = 1; // REPGAME_ITEM_TEST spawns are non-persistent
        float spin_seed = 0.0f;
        // Render chain entities (render_inited only): [0] is the sprite or
        // the first isometric box; multi-box blocks (stairs) use more.
        entt::entity entities[ STAIRS_MAX_BOXES ] = { entt::null, entt::null, entt::null };
        unsigned short face[ NUM_FACES_IN_CUBE ] = { 0 };   // resolved texture layers (isometric block items)

        bool pickup_pending = false; // PICKUP_REQUEST in flight; hidden
        // Multiplayer sync (net.id 0 / net.mirror false in single player):
        // mirrors skip physics/merge/despawn/aging and ease toward net.pos.
        // Ownership checks key off net.chunk (the chunk the server believes
        // the entity is in) so a crossing still streams to the server until
        // ENTITY_TRANSFER arrives.
        NetSync net;
        int net_sent_count = 0; // count streamed with the last ENTITY_STATE
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_block_object_instance, const VertexBufferLayout &vbl_item_instance );

    // Per-game-tick simulation. Frozen in capture mode, and per item while
    // the chunk at its center is not CHUNK_LIVE (age also freezes).
    void tick( RepGameState &state );

    // Spawns an item entity with its box bottom-centered on `pos`; vel is
    // blocks/tick. Returns its id (never 0): at the cap the oldest entity is
    // evicted. pickup_delay counts down in live ticks before the item can be
    // collected.
    uint32_t spawn( ItemID item, int count, const glm::dvec3 &pos, const glm::dvec3 &vel, int pickup_delay, int persistent = 1, bool from_net = false );

    // Pushes every live item away from `center`, linearly fading to 0 at
    // `radius` — same semantics as BlockBodies::impulse, stronger shove for
    // the lighter items.
    void impulse( const glm::dvec3 &center, double radius, double strength );

    // Rebuilds both instance buffers for this frame. alpha interpolates
    // prev_pos -> pos.
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    // The visual pose the renderer derives per item (spin about +y, rest
    // bob): shared by update_render and the mp_test dump so mirrors and
    // owners report comparable values.
    static void render_pose( const ItemEntity &it, float alpha, float *yaw, float *bob );
    void draw( const Renderer &renderer, const Shader &block_object_shader, const Shader &item_sprite_shader );

    // The in-hand item: same meshes and shaders as the dropped items —
    // extruded sprite for flat items, small cube for isometric block
    // icons — drawn in camera space from draw_ui_pass once depth is
    // cleared. set_held_item no-ops while the item is unchanged;
    // update_held takes the eye light sample (the camera-space poses ride
    // in u_MVP — the shaders already apply the per-instance transform).
    void set_held_item( ItemID item );
    void update_held( const glm::vec2 &light );
    void draw_held( const Renderer &renderer, const Shader &item_sprite_shader, const Shader &block_object_shader );

    size_t count( ) const { return this->items.size( ); }
    void cleanup( );
    // Test/debug: snapshot of every live item entity.
    std::vector<ItemEntity> debug_items( ) const { return this->items; }

    // Persistence — mirrors MobManager end to end: items_x_y_z files beside
    // mobs_x_y_z, pending_items drained on chunk adopt, a ~16-tick ownership
    // poll persisting items whose chunk left the active window, and
    // persist_all from World::cleanup. No reads/writes in capture mode, fly
    // mode, block world, or REPGAME_MOBS=0 / REPGAME_MOB_TEST runs.
    void init_storage( MapStorage *storage ) { this->storage = storage; }
    // Drain a just-finalized chunk's pending_items into live item entities.
    void adopt_chunk( Chunk &chunk );
    void poll_owned( World &world );
    void persist_all( );

    // A chunk this manager took ownership of via adopt_chunk: its items file
    // was loaded (or known absent) and its items persist back to disk when
    // the chunk leaves the active window or the world shuts down.
    struct OwnedItemChunk {
        glm::ivec3 pos;
        int file_present;
    };
    std::vector<OwnedItemChunk> owned_chunks;

    // ---- Multiplayer sync (NetEntities drives these; all no-ops single
    // player). Local items_* file reads/writes are off in multiplayer mode.
    void set_multiplayer_mode( bool mp_mode );
    bool multiplayer_mode( ) const { return this->mp_mode; }
    // Incoming record handlers (called by NetEntities).
    ItemEntity *find_by_net_id( NetEntityId net_id );
    void net_apply_spawn( const NetEntityRecord &rec, bool mirror );
    void net_apply_state( const NetEntityStateEntry &e );
    void net_apply_remove( NetEntityId net_id );
    // CHUNK_ROLE: drop the chunk (NONE) or re-sync it to the authoritative
    // record set (VIEWER -> mirrors, OWNER -> locally simulated).
    void net_apply_chunk_role( const glm::ivec3 &chunk, uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids );
    // ENTITY_TRANSFER adopt=0: become a mirror if our role still covers the
    // entity's chunk, else drop it.
    void net_release_entity( const NetEntityRecord &rec, bool still_viewed );
    // Owner-side outbound batch: push an ENTITY_STATE entry for every owned
    // synced item whose state changed since the last send (plus a once-a-
    // second keyframe). `tick_number` drives the keyframe cadence.
    void collect_net_state( NetEntities &net, std::vector<NetEntityStateEntry> &out, long tick_number );

    // rep_tests seams: bind the sync hub and mutate entities without a full
    // RepGameState tick.
    void test_bind_net( NetEntities *n ) { this->net = n; }
    std::vector<ItemEntity> &test_items( ) { return this->items; }

  private:
    void remove_item( size_t i );
    bool item_chunk_live( World &world, const ItemEntity &it ) const;
    uint32_t spawn_record( const NetEntityRecord &rec, bool mirror );
    // Send ENTITY_REMOVE for a synced entity we own (no-op otherwise).
    void send_net_remove( ItemEntity &it, uint8_t reason );
    void drop_render_instance( ItemEntity &it );

    MapStorage *storage = nullptr;
    // Bound to state.multiplayer.entities while multiplayer_mode — the sync
    // hub. net_id minting and sends route through it.
    NetEntities *net = nullptr;
    bool mp_mode = false;
    int poll_count = 0;

    std::vector<ItemEntity> items;
    uint32_t next_id = 1;
    long now_tick = 0;

    bool render_inited = false;
    // Isometric block items draw as a small textured cube shaped by the
    // block's scale/offset (block_object_vertex.glsl); everything else
    // draws as an extruded sprite.
    RenderChain<ParticleVertex, BlockObjectInstance> render_cube;
    RenderChain<ParticleVertex, ItemSpriteInstance> render_sprite;
    // Single-instance chains for the first-person held item (draw_ui_pass):
    // extruded sprite for flat items, cube for isometric block icons.
    RenderChain<ParticleVertex, ItemSpriteInstance> render_held;
    RenderChain<ParticleVertex, BlockObjectInstance> render_held_block;
    ItemID held_item_id = LAST_BLOCK_ID;
    entt::entity held_entity = entt::null;
    entt::entity held_block_entities[ STAIRS_MAX_BOXES ] = { entt::null, entt::null, entt::null };
};
