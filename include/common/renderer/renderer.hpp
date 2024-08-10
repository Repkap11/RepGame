#ifndef HEADER_RENDERER_H
#define HEADER_RENDERER_H

#include "common/renderer/vertex_buffer.hpp"
#include "common/renderer/vertex_array.hpp"
#include "common/renderer/index_buffer.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/renderer/shader.hpp"
#include "common/renderer/frame_buffer.hpp"

class Renderer {
  public:
    void draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const;
    void draw_lines( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const;
};
#endif