#ifndef HEADER_VERTEX_ARRAY_H
#define HEADER_VERTEX_ARRAY_H

#include "abstract/vertex_buffer.hpp"
#include "abstract/vertex_buffer_layout.hpp"

typedef struct {
    unsigned int mRendererId;
} VertexArray;

void vertex_array_init( VertexArray *vertexArray );
void vertex_array_bind( const VertexArray *vertexArray );
void vertex_array_add_buffer( VertexArray *vertexArray, const VertexBuffer *vertexBuffer, const VertexBufferLayout *vertexBufferLayout, unsigned int divisor, unsigned int stride );
void vertex_array_destroy( VertexArray *vertexArray );

#endif