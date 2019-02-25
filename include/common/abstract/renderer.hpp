#ifndef HEADER_RENDERER_H
#define HEADER_RENDERER_H

#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/vertex_array.hpp"
#include "common/abstract/index_buffer.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"
#include "common/abstract/shader.hpp"

typedef struct { unsigned int mRendererId; } Renderer;

void renderer_draw( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader, unsigned int num_instances );
void renderer_draw_lines( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader, unsigned int num_instances );
#endif