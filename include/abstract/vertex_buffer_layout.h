#ifndef HEADER_VERTEX_BUFFER_LAYOUT_H
#define HEADER_VERTEX_BUFFER_LAYOUT_H

#define NUM_VERTEX_BUFFER_ELEMENTS 4

typedef struct {
    unsigned int type;
    unsigned int count;
    unsigned char normalized;
} VertexBufferElement;

typedef struct {
    unsigned int mRendererId;
    unsigned int current_size;
    unsigned int stride;
    VertexBufferElement elements[ NUM_VERTEX_BUFFER_ELEMENTS ];
} VertexBufferLayout;

unsigned int vertex_buffer_layout_size_of_type( unsigned int type );

void vertex_buffer_layout_init( VertexBufferLayout *vertexBufferLayout );
void vertex_buffer_layout_init_element( VertexBufferElement *vertexBufferElement, unsigned int type, unsigned int count, unsigned int normalized );
void vertex_buffer_layout_bind( const VertexBufferLayout *vertexBufferLayout );

void vertex_buffer_layout_push_float( VertexBufferLayout *vertexBufferLayout, unsigned int count );
void vertex_buffer_layout_push_unsigned_int( VertexBufferLayout *vertexBufferLayout, unsigned int count );
void vertex_buffer_layout_push_int( VertexBufferLayout *vertexBufferLayout, unsigned int count );
void vertex_buffer_layout_push_bytes( VertexBufferLayout *vertexBufferLayout, unsigned int count );

void vertex_buffer_layout_unbind( const VertexBufferLayout *vertexBufferLayout );
void vertex_buffer_layout_destroy( const VertexBufferLayout *vertexBufferLayout );

#endif