#pragma once

#include <vector>

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"

// Heart particles puffed above breeding mobs. Same write-once GPU animation
// model as BlockDebris — the vertex shader computes rise/sway/billboard from
// u_Time, so the only CPU work is creating entities on spawn and sweeping
// expired ones in draw(). Draws through object_fragment.glsl like debris;
// the instance buffer reuses the DebrisInstance layout (velocity is the
// drift, texel is unused — the heart sprite maps the whole quad).
class HeartParticles {
    struct LiveParticle {
        entt::entity entity;
        float spawn_time;
        float life;
    };
    std::vector<LiveParticle> live;
    RenderChain<ParticleVertex, DebrisInstance> render_chain;
    bool inited = false;

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_debris_instance );
    // The u_Time clock the shader animates against (fmod(now, 3600)). Draw
    // callers pass that frame's time_s to spawn/draw so spawn stamps match
    // the frame's u_Time exactly — a mid-frame sample would skew negative.
    static float now( );
    // Whether a particle stamped `spawn_time` has outlived `life` at `time_s`
    // — tolerates spawn stamps marginally ahead of time_s (only a diff past
    // half the wrap period is a real rollover).
    static bool expired( float time_s, float spawn_time, float life );
    // Burst of `count` hearts rising from pos, stamped with the frame's
    // time_s. `light` is the (sky, block) flood-fill sample baked into every
    // spawned instance.
    void spawn( const glm::dvec3 &pos, const glm::vec2 &light, int count, float time_s );
    void draw( const Renderer &renderer, const Shader &shader, float time_s );
    size_t count( ) { return this->render_chain.length( ); }
    void cleanup( );
};
