#ifndef HEADER_MOBS_H
#define HEADER_MOBS_H

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"

typedef RenderChain<ParticleVertex, ParticlePosition> MobsRenderChain;

class MultiplayerAvatars {
    entt::entity entity_map[ MAX_CLIENT_FDS ];
    glm::mat4 initial_mat;
    MobsRenderChain render_chain;

    void init_particle( ParticlePosition &mob );

  public:
    void init( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_position );
    void add( unsigned int particle_id );
    void update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( unsigned int particle_id );
    void draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader );
    void cleanup( );
};

#endif