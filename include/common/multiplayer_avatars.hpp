#pragma once

#include "common/Particle.hpp"
#include "common/RenderChain.hpp"

class ChunkLoader;

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
    // Re-sample the world light field at each avatar's position. Runs per
    // frame (avatars get no updates while standing still, but torches move).
    void update_lighting( const ChunkLoader &chunk_loader );
    void draw( const Renderer &renderer, const Shader &shader );
    void cleanup( );
};