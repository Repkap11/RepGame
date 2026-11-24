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
    Texture texture_end_noise;
    Texture texture_end_stars;
    RenderChain<ParticleVertex, ParticlePosition> render_chain_sky_box;
    glm::vec3 m_avgColor = glm::vec3( 0.5f );
    glm::vec3 m_avgColorNether = glm::vec3( 0.2f, 0.03f, 0.02f );
    // end_noise mean (114.8, 87.6, 155.9)/255 times the shader's 0.157 tint.
    glm::vec3 m_avgColorEnd = glm::vec3( 0.071f, 0.054f, 0.096f );
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
        return this->m_dimension == DIM_NETHER ? this->texture_nether : this->m_dimension == DIM_END ? this->texture_end_noise : this->texture;
    }
    // The end portal/gateway screen-space starfield (chunk_fragment.glsl)
    // samples these two tiling noises: end_noise as the dark background
    // layer, end_stars for the additive parallax layers.
    const Texture &get_end_noise_texture( ) const {
        return this->texture_end_noise;
    }
    const Texture &get_end_stars_texture( ) const {
        return this->texture_end_stars;
    }
    const glm::vec3 &get_avg_color( ) const {
        return this->m_dimension == DIM_NETHER ? this->m_avgColorNether : this->m_dimension == DIM_END ? this->m_avgColorEnd : this->m_avgColor;
    }
};
