#pragma once

#include <random>
#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/mob_types.hpp"
#include "common/utils/map_storage.hpp"

class World;
class Chunk;
class ChunkLoader;

#define MAX_MOBS 256

// Client-local passive mobs. Simulation (physics, wander AI) runs on the game
// tick; rendering rebuilds one instanced RenderChain<ParticleVertex,
// MobPartInstance> per frame with CPU-evaluated bone transforms from the
// generated mob_models[] table. REPGAME_MOBS=0 disables everything;
// REPGAME_MOB_TEST="type,x,y,z,yaw;..." spawns exact mobs for captures;
// capture mode freezes AI and physics.
class MobManager {
    friend int test_mobs( );
    struct Mob {
        MobType type;
        int variant;
        glm::dvec3 pos;      // feet
        glm::dvec3 prev_pos; // previous tick's feet, for render interpolation
        float yaw;           // facing, degrees; forward = (sin(yaw), 0, -cos(yaw))
        float prev_yaw;
        glm::dvec3 vel;      // blocks/tick; y is gravity-owned like the player's y_speed
        int standing;
        float health;
        int ai_state;   // 0 = idle, 1 = wandering
        int ai_ticks;   // idle countdown or wander timeout
        int panic_ticks;
        glm::dvec3 target; // wander destination
        int hurt_ticks;
        int death_ticks; // >0 while the 0.5s tip-over plays; removed at the end
        float walk_dist; // horizontal distance walked, drives the gait phase
        float leg_amp;   // eased 0..1 gait amplitude
        float head_yaw;  // eased look-at offsets on the head bone, degrees
        float head_pitch;
        int persistent;   // REPGAME_MOB_TEST spawns are non-persistent
        int say_ticks;    // ambient "say" countdown
        float step_acc;   // horizontal distance since the last step sound
        glm::dvec3 kb;    // knockback impulse, decays ~x0.85/tick
        std::vector<entt::entity> parts; // one render entity per model cube
    };
    // A chunk this manager took ownership of via adopt_chunk: its mobs file
    // was loaded (or the natural-spawn roll ran) and its mobs persist back to
    // the same file when the chunk leaves the active window.
    struct OwnedChunk {
        glm::ivec3 pos;
        int file_present;
        int spawned;
    };
    bool enabled;
    bool test_mode; // REPGAME_MOB_TEST set: no natural spawn/load/persist
    bool render_inited;
    std::vector<Mob> mobs;
    std::vector<OwnedChunk> owned_chunks;
    std::mt19937 rng;
    MapStorage *storage;
    long poll_count;
    RenderChain<ParticleVertex, MobPartInstance> render_chain;

    void tick_mob( World &world, Mob &mob, const MobTypeDef &def, const glm::dvec3 &player_pos );
    void poll_owned( World &world );
    std::vector<Mob>::iterator remove_mob( std::vector<Mob>::iterator it );

  public:
    MobManager( );
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance );
    void init_storage( MapStorage *storage ) { this->storage = storage; }
    bool spawn( MobType type, const glm::dvec3 &pos, float yaw, int persistent = 1 );
    // Spawn `type` three blocks in front of `camera_pos` along `look`, dropped
    // onto the ground (persistent). Debug spawn key.
    bool spawn_in_front( MobType type, const glm::dvec3 &camera_pos, const glm::dvec3 &look, World &world );
    // AABB slab test against every live (non-dying) mob's hitbox. Returns the
    // mob index or -1; *out_dist gets the entry distance when hit.
    int raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const;
    // The crosshair's mob target: the ray's mob hit when its entry distance
    // is nearer than the selected block cell's (or no block is selected),
    // else -1. -1 as well when mobs are disabled.
    int resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                        int selectionInBounds, const glm::ivec3 &pos_destroy, double *out_dist ) const;
    // Apply damage + knockback away from attacker_pos; returns true if this
    // hit started the death animation.
    bool hurt( int index, float damage, const glm::dvec3 &attacker_pos );
    const MobTypeDef &def_at( int index ) const { return mob_types[ this->mobs[ index ].type ]; }
    // Drain a just-finalized chunk's pending_mobs into live mobs and record
    // ownership. Skips chunks already owned, parked/loading leftovers, and
    // test mode.
    void adopt_chunk( Chunk &chunk );
    void tick( World &world, const glm::dvec3 &player_pos );
    void update_render( float alpha, const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &shader );
    // Persist every owned chunk's mobs (World::cleanup).
    void persist_all( );
    size_t count( ) const { return this->mobs.size( ); }
    bool is_enabled( ) const { return this->enabled; }
    void cleanup( );
};
