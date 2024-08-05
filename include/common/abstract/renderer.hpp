#ifndef HEADER_RENDERER_H
#define HEADER_RENDERER_H

#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/vertex_array.hpp"
#include "common/abstract/index_buffer.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"
#include "common/abstract/shader.hpp"
#include "common/abstract/frame_buffer.hpp"

class Renderer {
  public:
    void draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const;
    void draw_lines( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const;
};
#endif