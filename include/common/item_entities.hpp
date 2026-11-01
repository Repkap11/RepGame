#pragma once

#include <cstdint>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/item.hpp"
#include "common/utils/collision.hpp"

class ChunkLoader;
class World;
class Chunk;
class MapStorage;
struct RepGameState;

// Per-instance data for the extruded-sprite item draw: one shared 66-quad
// mesh (item_entities.cpp) is instanced per item, sampling a single atlas
// layer in item_sprite_vertex.glsl. The transform is rebuilt each frame by
// ItemEntities::update_render (spin + bob on the CPU).
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
        entt::entity entity = entt::null;                   // render chain entity (render_inited only)
        unsigned short face[ NUM_FACES_IN_CUBE ] = { 0 };   // resolved texture layers (isometric block items)
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position, const VertexBufferLayout &vbl_item_instance );

    // Per-game-tick simulation. Frozen in capture mode, and per item while
    // the chunk at its center is not CHUNK_LIVE (age also freezes).
    void tick( RepGameState &state );

    // Spawns an item entity with its box bottom-centered on `pos`; vel is
    // blocks/tick. Returns its id (never 0): at the cap the oldest entity is
    // evicted. pickup_delay counts down in live ticks before the item can be
    // collected.
    uint32_t spawn( ItemID item, int count, const glm::dvec3 &pos, const glm::dvec3 &vel, int pickup_delay, int persistent = 1 );

    // Pushes every live item away from `center`, linearly fading to 0 at
    // `radius` — same semantics as BlockBodies::impulse, stronger shove for
    // the lighter items.
    void impulse( const glm::dvec3 &center, double radius, double strength );

    // Rebuilds both instance buffers for this frame. alpha interpolates
    // prev_pos -> pos.
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &object_shader, const Shader &item_sprite_shader );

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

  private:
    void remove_item( size_t i );
    bool item_chunk_live( World &world, const ItemEntity &it ) const;

    MapStorage *storage = nullptr;
    int poll_count = 0;

    std::vector<ItemEntity> items;
    uint32_t next_id = 1;
    long now_tick = 0;

    bool render_inited = false;
    // Isometric block items draw as a small textured cube through the
    // falling-block path; everything else draws as an extruded sprite.
    RenderChain<ParticleVertex, ParticlePosition> render_cube;
    RenderChain<ParticleVertex, ItemSpriteInstance> render_sprite;
};
