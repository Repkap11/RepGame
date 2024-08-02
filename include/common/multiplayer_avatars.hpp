#ifndef HEADER_MOBS_H
#define HEADER_MOBS_H

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"

typedef RenderChain<ParticleVertex, ParticlePosition> MobsRenderChain;

class MultiplayerAvatars {
    MobsRenderChain *render_chain;
    entt::entity entity_map[ MAX_CLIENT_FDS ];
    glm::mat4 initial_mat;

    void init_particle( ParticlePosition &mob );

  public:
    void init( MobsRenderChain *render_chain );
    void add( unsigned int particle_id );
    void update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( unsigned int particle_id );
};

#endif