#pragma once


#include "common/renderer/vertex_buffer.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"

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