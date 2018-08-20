#ifndef HEADER_VERTEX_BUFFER_H
#define HEADER_VERTEX_BUFFER_H

typedef struct {
    unsigned int mRendererId;
} VertexBuffer;

void vertex_buffer_init( VertexBuffer *vertexBuffer );
void vertex_buffer_destroy( const VertexBuffer *vertexBuffer );
void vertex_buffer_bind( const VertexBuffer *vertexBuffer );
void vertex_buffer_unbind( const VertexBuffer *vertexBuffer );
void vertex_buffer_set_data( VertexBuffer *vertexBuffer, const void *data, unsigned int size );

#endif