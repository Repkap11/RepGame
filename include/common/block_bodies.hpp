#pragma once

#include <cstdint>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/collision.hpp"

class ChunkLoader;
class World;
struct RepGameState;

// Tick-driven CPU physics bodies, used both for falling blocks (sand,
// gravel, concrete powder, anvils) and for ignited TNT — replacing the old
// GPU-ballistic PrimedTnt path. Each body is an axis-aligned box (no
// rotation) integrated every game tick against the live voxels and against
// the other bodies. The two kinds differ only in how they end: FALLING
// bodies come to rest, snap to the grid and are written back as blocks;
// PRIMED_TNT never settles — it rests, slides and gets shoved until its
// fuse's TntExplodeEvent calls take() and detonates where it actually is.
//
// The simulation never touches GL, so rep_tests run it headless; the render
// chains are only used after init() (the render_inited pattern from
// MobManager).
class BlockBodies {
  public:
    enum BodyKind { BODY_FALLING, BODY_PRIMED_TNT };

    // Cube edge length — slightly under a full block like MC's entities.
    static constexpr double BODY_SIZE = 0.98;
    static constexpr size_t MAX_BLOCK_BODIES = 1024;

    struct Body {
        uint32_t id = 0; // stable, non-zero; 0 means "no body"
        BodyKind kind;
        BlockState state = BLOCK_STATE_AIR;
        float mass = 1.0f;
        glm::dvec3 min = glm::dvec3( 0.0 );
        glm::dvec3 prev_min = glm::dvec3( 0.0 );
        glm::dvec3 vel = glm::dvec3( 0.0 ); // blocks/tick
        int grounded = 0;
        int settle_ticks = 0;
        long fuse_start_tick = 0;
        long fuse_ticks = 0;
        entt::entity entity = entt::null;                          // render chain entity (render_inited only)
        unsigned short face[ NUM_FACES_IN_CUBE ] = { 0 };          // resolved texture layers
    };

    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position, const VertexBufferLayout &vbl_primed_tnt_instance );

    // Per-game-tick simulation. Frozen in capture mode, and per body while
    // the chunk at its center is not CHUNK_LIVE.
    void tick( RepGameState &state );

    // Spawns a FALLING body occupying `cell`. Returns its id, or 0 at the
    // body cap — the caller must then leave the block in the world.
    uint32_t spawn_falling( const glm::ivec3 &cell, const BlockState &state, const glm::dvec3 &vel );
    // Spawns a PRIMED_TNT body centered on `center`. vel is blocks/tick.
    // Always succeeds: at the cap it evicts the oldest FALLING body, which
    // lands where it is if it can and otherwise breaks into debris.
    uint32_t spawn_primed_tnt( RepGameState &state, const glm::dvec3 &center, const glm::dvec3 &vel, BlockID id, long fuse_ticks, long now_tick );
    // Removes the body and returns its current center. False when the id is
    // not live (id 0 — e.g. a creeper blast — or already gone).
    bool take( uint32_t id, glm::dvec3 *center );
    // Pushes every live body away from `center`, linearly fading to 0 at
    // `radius`.
    void impulse( const glm::dvec3 &center, double radius, double strength );
    // At exit: places FALLING bodies at their cell if it is free and loaded,
    // and puts PRIMED_TNT back as an unlit block, so nothing is lost on save.
    void land_all( RepGameState &state );

    // Rebuilds both instance buffers for this frame (called where
    // mobs.update_render runs). alpha interpolates prev_min -> min.
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &object_shader, const Shader &primed_tnt_shader );

    size_t count_falling( ) const;
    size_t count_tnt( ) const;
    void cleanup( );
    // Test/debug: snapshot of every live body.
    std::vector<Body> debug_bodies( ) const { return this->bodies; }

  private:
    // Writes the body back into the world at its rest cell (set block +
    // multiplayer + neighbor updates + debris for a replaced plant), or
    // breaks it into debris when the cell holds a non-colliding
    // non-replaceable block. Returns true when the body is consumed.
    bool settle_land( RepGameState &state, Body &b );
    // After a body lands and writes a full 1.0 block at `cell`, bodies that
    // were resting on it (their 0.98 box sat ~0.02 inside the new block)
    // would sink through — the collision clamp only fires on face-crossing.
    // Lift every body whose footprint overlaps the cell and whose bottom is
    // embedded in it; lifted bodies cascade upward as new pushers. `skip_id`
    // excludes the body being landed (still in this->bodies during tick).
    void lift_bodies_above( const glm::ivec3 &cell, uint32_t skip_id );
    void remove_body( size_t i );
    bool body_chunk_live( World &world, const Body &b ) const;

    std::vector<Body> bodies;
    uint32_t next_id = 1;
    // Latest tick number, recorded in tick() so update_render can compute
    // the fuse age for the flash.
    long now_tick = 0;

    // Bodies that landed but whose target chunk may not have remeshed yet
    // (repop_must budget overflow): still drawn, no longer colliding, dropped
    // in update_render once the chunk's needs_repopulation clears.
    struct Landed {
        glm::dvec3 min;
        glm::ivec3 cell;
        unsigned short face[ NUM_FACES_IN_CUBE ];
        entt::entity entity;
    };
    std::vector<Landed> landed;

    bool render_inited = false;
    RenderChain<ParticleVertex, ParticlePosition> render_falling;
    RenderChain<ParticleVertex, PrimedTntInstance> render_tnt;
};
