#ifndef HEADER_VERTEX_ARRAY_H
#define HEADER_VERTEX_ARRAY_H

#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"

class VertexArray {
    unsigned int mRendererId;
    unsigned int divisor;
    unsigned int stride;

  public:
    void init( );
    void bind( ) const;
    void add_buffer( const VertexBuffer &vertexBuffer, const VertexBufferLayout &vertexBufferLayout );
    void unbind( ) const;
    void destroy( );
};

#endif