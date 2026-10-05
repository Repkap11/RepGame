#pragma once


#include "common/renderer/vertex_buffer.hpp"
#include "common/renderer/vertex_array.hpp"
#include "common/renderer/index_buffer.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/renderer/shader.hpp"
#include "common/renderer/frame_buffer.hpp"

class Renderer {
  public:
    void draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const;
    // Draws a sub-range of indexBuffer (index_offset in bytes). Lets many
    // objects share one element buffer, e.g. the per-visibility-mask chunk IB.
    void draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances,
               unsigned int index_count, uintptr_t index_offset ) const;
};