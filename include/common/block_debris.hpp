#pragma once

#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"
#include "common/block_definitions.hpp"

// GPU-animated debris particles spawned when blocks break. Instances are
// write-once (position/rotation computed in debris_vertex.glsl from u_Time),
// so the only CPU work is creating entities on break and sweeping expired
// ones in draw(). Draws through the same object_fragment.glsl path as mobs.
class BlockDebris {
    struct LiveParticle {
        entt::entity entity;
        float spawn_time;
        float life;
    };
    std::vector<LiveParticle> live;
    RenderChain<ParticleVertex, DebrisInstance> render_chain;

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_debris_instance );
    void spawn_block_break( const glm::ivec3 &block_pos, BlockID block_id, float time_s );
    void draw( const Renderer &renderer, const Shader &shader, float time_s );
    void cleanup( );
};
