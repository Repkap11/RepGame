#ifndef HEADER_ECS_RENDERER_H
#define HEADER_ECS_RENDERER_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/textures.hpp"
#include "abstract/renderer.hpp"
#include "block.hpp"
#include "chunk.hpp"
#include "common/Particle.hpp"
#include <entt/entt.hpp>

class ECS_Renderer {
  private:
    entt::registry registry;
    IndexBuffer ib;
    VertexArray va;
    VertexBuffer vb_particle_placement;
    VertexBuffer vb_particle_shape;
    glm::mat4 initial_mat;
    bool vertex_buffer_dirty;
    unsigned int vertex_buffer_particle_count;

    std::map<unsigned int, entt::entity> entity_map;

    void on_construct( entt::registry &registry, entt::entity entity );
    void on_update( entt::registry &registry, entt::entity entity );
    void on_destroy( entt::registry &registry, entt::entity entity );

  public:
    ECS_Renderer( );
    ECS_Renderer( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement );
    void add( unsigned int particle_id );
    void update_position( int particle_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( unsigned int particle_id );
    void draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader );
    void cleanup( );
    int check_consistency( );

  protected:
    virtual void init_particle( ParticlePosition *particle, int particle_id ) = 0;
};

#endif