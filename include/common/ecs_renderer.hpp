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
#include <entt/entity/registry.hpp>
#include <map>

class ECS_Renderer {
  private:
    entt::registry registry;
    IndexBuffer ib_particle;
    IndexBuffer ib_face;
    VertexArray va_particle;
    VertexArray va_face;
    VertexBuffer vb_particle_placement;
    VertexBuffer vb_face_placement;
    VertexBuffer vb_particle_shape;
    VertexBuffer vb_face_shape;
    glm::mat4 initial_mat;
    bool vertex_buffer_dirty;

    void on_construct( entt::registry &registry, entt::entity entity );
    void on_update( entt::registry &registry, entt::entity entity );
    void on_destroy( entt::registry &registry, entt::entity entity );

  public:
    ECS_Renderer( );
    void init( VertexBufferLayout *vbl_object_vertex, VertexBufferLayout *vbl_object_placement );

    const std::pair<entt::entity, std::reference_wrapper<ParticlePosition>> create_particle( long lifetime );
    const std::pair<entt::entity, std::reference_wrapper<ParticlePosition>> create_face( long lifetime );
    void update_position( const entt::entity &entity, float x, float y, float z, const glm::mat4 &rotation );
    void remove( const entt::entity &entity );

    void draw( const glm::mat4 &mvp, Renderer *renderer, Shader *shader );
    void cleanup( );
};

#endif