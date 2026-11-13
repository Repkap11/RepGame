#pragma once

#include <cstdint>
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
// and curves — with MC-style physics: powered golden rails boost, dead
// ones brake, slopes accelerate downhill, empty carts lose more speed than
// ridden ones. Derailed carts fall through the world with move_box
// collision until they re-snap onto a rail or rest on the ground.
//
// Local to the client in this phase: multiplayer mode refuses spawns and
// does no carts_* file IO. Riding (mount/dismount/camera) is Phase C —
// the physics already honours a rider flag and a push input, but nothing
// sets them yet.
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
        NetSync net;                   // reserved for future MP; unused
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
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance );
    void init_storage( MapStorage *s );

    // Spawns a cart bottom-centred on `pos`, snapping onto a rail there.
    // Returns its id (never 0); 0 at the cap or in multiplayer mode.
    uint32_t spawn( const glm::dvec3 &pos, float yaw, int persistent = 1 );
    // Right-click with MINECART on a rail cell: centre of cell, on the
    // rail, yaw along the rail axis nearest player_yaw.
    uint32_t spawn_on_rail( World &world, const glm::ivec3 &rail_cell, float player_yaw );

    // Per-game-tick simulation. Frozen in capture mode, and per cart while
    // the chunk at its position is not CHUNK_LIVE.
    void tick( RepGameState &state, const CartTickInput &in );

    // Nearest cart the ray hits within max_dist, or -1; *out_dist takes
    // the hit distance (like MobManager::raycast).
    int raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const;
    // Nearest cart nearer than the selected block, or -1 — the cart
    // version of MobManager::resolve_target.
    int resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                        int selectionInBounds, const glm::ivec3 &pos_destroy, double *out_dist ) const;

    // Hit: creative removes outright; survival adds decaying damage so
    // three quick hits break. Returns true when the cart was removed;
    // *drop set true when the caller should drop a MINECART item.
    bool hurt( int index, bool survival, bool *drop );

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

    // Multiplayer: carts are local-only for now — mp mode refuses spawns
    // and disables local file IO (NetSync on Cart is reserved).
    void set_multiplayer_mode( bool mp_mode ) { this->mp_mode = mp_mode; }
    bool multiplayer_mode( ) const { return this->mp_mode; }

    // Rebuilds the instance buffer for this frame. alpha interpolates
    // prev_pos/prev_yaw/prev_pitch.
    void update_render( float alpha, const ChunkLoader &loader );
    void draw( const Renderer &renderer, const Shader &mob_shader );

    size_t count( ) const { return this->carts.size( ); }
    std::vector<Cart> debug_carts( ) const { return this->carts; }
    void cleanup( );

    // rep_tests seam: mutate entities without a full RepGameState tick.
    std::vector<Cart> &test_carts( ) { return this->carts; }

  private:
    void remove_cart( size_t i );
    bool cart_chunk_live( World &world, const Cart &c ) const;
    void poll_owned( World &world );
    void drop_render_parts( Cart &c );

    MapStorage *storage = nullptr;
    bool mp_mode = false;
    int poll_count = 0;

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
