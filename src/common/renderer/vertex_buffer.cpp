#include "common/RepGame.hpp"
#include "common/renderer/vertex_buffer.hpp"

void VertexBuffer::init( ) {
    glGenBuffers( 1, &this->mRendererId );
    VertexBuffer::bind( );
}

void VertexBuffer::set_data( const void *data, unsigned int size ) {
    VertexBuffer::bind( );
    if ( this->size != size ) {
        glBufferData( GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW );
        this->size = size;
    } else {
        glBufferSubData( GL_ARRAY_BUFFER, 0, size, data );
    }
}

void VertexBuffer::set_subdata( const void *data, unsigned int offset, unsigned int size ) {
    VertexBuffer::bind( );
    glBufferSubData( GL_ARRAY_BUFFER, offset, size, data );
}

void VertexBuffer::destroy( ) {
    this->size = 0;
    this->mRendererId = 0;
    glDeleteBuffers( 1, &this->mRendererId );
}

void VertexBuffer::bind( ) const {
    glBindBuffer( GL_ARRAY_BUFFER, this->mRendererId );
}

void VertexBuffer::unbind( ) const {
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
}