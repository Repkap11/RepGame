#pragma once

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"

class MultiplayerAvatars {
    entt::entity entity_map[ MAX_CLIENT_FDS ];
    glm::mat4 initial_mat;
    RenderChain<ParticleVertex, ParticlePosition> render_chain;

    static void init_particle( ParticlePosition &mob );

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position );
    void add( unsigned int particle_id );
    void update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( unsigned int particle_id );
    void draw( const Renderer &renderer, const Shader &shader );
    void cleanup( );
};