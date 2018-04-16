#ifndef HEADER_VERTEX_ARRAY_H
#define HEADER_VERTEX_ARRAY_H

#include "abstract/vertex_buffer.h"
#include "abstract/vertex_buffer_layout.h"

typedef struct {
    unsigned int mRendererId;
} VertexArray;

void vertex_array_init(VertexArray* vertexArray);
void vertex_array_bind( const VertexArray *vertexArray );
void vertex_array_add_buffer(VertexArray* vertexArray, const VertexBuffer* vertexBuffer, const VertexBufferLayout* vertexBufferLayout);
void vertex_array_destroy(VertexArray* vertexArray);

#endif