#ifndef HEADER_RENDERER_H
#define HEADER_RENDERER_H

#include "abstract/vertex_buffer.h"
#include "abstract/vertex_array.h"
#include "abstract/index_buffer.h"
#include "abstract/vertex_buffer_layout.h"
#include "abstract/shader.h"

typedef struct { unsigned int mRendererId; } Renderer;

void renderer_draw( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader );
void renderer_clear( const Renderer *renderer );

#endif