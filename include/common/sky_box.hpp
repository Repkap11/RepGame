#ifndef HEADER_SKY_BOX_H
#define HEADER_SKY_BOX_H

#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/renderer.hpp"
#include "common/Particle.hpp"

class SkyBox {
    Texture texture;
    RenderChain<ParticleVertex, ParticlePosition> render_chain_sky_box;

  public:
    void init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_object_position );
    void draw( const Renderer &renderer, Shader &shader );
    void destroy( );
};

#endif