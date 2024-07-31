#include "common/RepGame.hpp"
#include "common/abstract/vertex_buffer.hpp"

void vertex_buffer_init( VertexBuffer *vertexBuffer ) {
    glGenBuffers( 1, &vertexBuffer->mRendererId );
    vertex_buffer_bind( vertexBuffer );
}

void vertex_buffer_set_data( VertexBuffer *vertexBuffer, const void *data, unsigned int size ) {
    vertex_buffer_bind( vertexBuffer );
    glBufferData( GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW );
}

void vertex_buffer_set_subdata( VertexBuffer *vertexBuffer, const void *data, unsigned int offset, unsigned int size ) {
    vertex_buffer_bind( vertexBuffer );
    glBufferSubData( GL_ARRAY_BUFFER, offset, size, data );
}

void vertex_buffer_destroy( const VertexBuffer *vertexBuffer ) {
    glDeleteBuffers( 1, &( vertexBuffer->mRendererId ) );
}

void vertex_buffer_bind( const VertexBuffer *vertexBuffer ) {
    glBindBuffer( GL_ARRAY_BUFFER, vertexBuffer->mRendererId );
}

void vertex_buffer_unbind( const VertexBuffer *vertexBuffer ) {
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
}