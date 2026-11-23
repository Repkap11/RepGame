#pragma once

#include "renderer/index_buffer.hpp"
#include "renderer/vertex_buffer.hpp"
#include "renderer/vertex_array.hpp"
#include "renderer/renderer.hpp"
#include "common/RenderChain.hpp"
#include "common/Particle.hpp"
#include "common/dimension.hpp"

class SkyBox {
    Texture texture;
    Texture texture_nether;
    Texture texture_end;
    RenderChain<ParticleVertex, ParticlePosition> render_chain_sky_box;
    glm::vec3 m_avgColor = glm::vec3( 0.5f );
    glm::vec3 m_avgColorNether = glm::vec3( 0.2f, 0.03f, 0.02f );
    glm::vec3 m_avgColorEnd = glm::vec3( 0.10f, 0.06f, 0.15f );
    DimensionId m_dimension = DIM_OVERWORLD;

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position );
    // Switch the panorama + fog average for the camera's dimension. The
    // nether has no sky — the backdrop is a dim ember haze — but the mesh
    // still draws so gaps in the terrain never show stale framebuffer.
    void set_dimension( DimensionId dim );
    void draw( const Renderer &renderer, Shader &shader );
    void destroy( );
    const Texture &get_texture( ) const {
        return this->m_dimension == DIM_NETHER ? this->texture_nether : this->m_dimension == DIM_END ? this->texture_end : this->texture;
    }
    const glm::vec3 &get_avg_color( ) const {
        return this->m_dimension == DIM_NETHER ? this->m_avgColorNether : this->m_dimension == DIM_END ? this->m_avgColorEnd : this->m_avgColor;
    }
};
