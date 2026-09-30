#pragma once

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include "common/Particle.hpp"

class SkyBox {
    Texture texture;
    RenderChain<ParticleVertex, ParticlePosition> render_chain_sky_box;
    glm::vec3 m_avgColor = glm::vec3( 0.5f );
    glm::vec3 m_sunDir = glm::normalize( glm::vec3( -0.6f, 0.5f, -0.2f ) );

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position );
    void draw( const Renderer &renderer, Shader &shader );
    void destroy( );
    const Texture &get_texture( ) const { return this->texture; }
    const glm::vec3 &get_avg_color( ) const { return this->m_avgColor; }
    const glm::vec3 &get_sun_dir( ) const { return this->m_sunDir; }
};