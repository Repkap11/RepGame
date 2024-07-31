#ifndef HEADER_VERTEX_BUFFER_H
#define HEADER_VERTEX_BUFFER_H

typedef struct {
    unsigned int mRendererId;
    unsigned int size;
} VertexBuffer;

void vertex_buffer_init( VertexBuffer *vertexBuffer );
void vertex_buffer_destroy( VertexBuffer *vertexBuffer );
void vertex_buffer_bind( const VertexBuffer *vertexBuffer );
void vertex_buffer_unbind( const VertexBuffer *vertexBuffer );
void vertex_buffer_set_data( VertexBuffer *vertexBuffer, const void *data, unsigned int size );
void vertex_buffer_set_subdata( VertexBuffer *vertexBuffer, const void *data, unsigned int offset, unsigned int size );

#endif