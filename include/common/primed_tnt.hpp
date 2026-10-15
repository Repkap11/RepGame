#pragma once

#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/block_definitions.hpp"

class ChunkLoader;

// GPU-animated primed TNT entities spawned when TNT ignites. Same write-once
// pattern as BlockDebris: position/flash are computed in
// primed_tnt_vertex.glsl from u_Time, so the only CPU work is creating the
// entity on ignite and sweeping it once its fuse ends. Draws through
// object_fragment.glsl like debris and avatars.
class PrimedTnt {
    struct LivePrimed {
        entt::entity entity;
        float spawn_time;
        float life;
    };
    std::vector<LivePrimed> live;
    RenderChain<ParticleVertex, PrimedTntInstance> render_chain;

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_primed_tnt_instance );
    // center is the world-space cube center; ground_t clamps the ballistic
    // flight at the predicted terrain-impact time (the entity rests there
    // until the fuse ends). light is the flood-fill sample at the cell.
    // Returns the entity so the fuse event can despawn it on detonation.
    entt::entity spawn( const glm::vec3 &center, const glm::vec3 &velocity, float life_s, float ground_t, const glm::vec2 &light );
    // Removes the entity if it is still live — no-op when the fuse sweep
    // already reclaimed it.
    void despawn( entt::entity entity );
    void update_lighting( const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &shader, float time_s );
    // Live instance count, for the debug overlay.
    size_t count( ) { return this->render_chain.length( ); }
    void cleanup( );
};
