#ifndef HEADER_RENDERER_H
#define HEADER_RENDERER_H

#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_array.hpp"
#include "abstract/index_buffer.hpp"
#include "abstract/vertex_buffer_layout.hpp"
#include "abstract/shader.hpp"

typedef struct { unsigned int mRendererId; } Renderer;

void renderer_draw( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader, unsigned int num_instances );
void renderer_clear( const Renderer *renderer );

#endif