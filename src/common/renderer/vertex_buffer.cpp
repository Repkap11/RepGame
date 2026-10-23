#include "common/RepGame.hpp"
#include "common/renderer/vertex_buffer.hpp"

void VertexBuffer::init( ) {
    glGenBuffers( 1, &this->mRendererId );
    VertexBuffer::bind( );
}

void VertexBuffer::set_data( const void *data, unsigned int size ) {
    VertexBuffer::bind( );
    // A 0-byte glBufferData is a no-op semantically, but the PowerVR GLES
    // driver reports GL_OUT_OF_MEMORY for it.
    if ( size == 0 ) {
        return;
    }
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

void VertexBuffer::reserve( const unsigned int size ) {
    VertexBuffer::bind( );
    if ( this->size != size ) {
        glBufferData( GL_ARRAY_BUFFER, size, NULL, GL_STATIC_DRAW );
        this->size = size;
    }
}

void VertexBuffer::destroy( ) {
    this->size = 0;
    glDeleteBuffers( 1, &this->mRendererId );
    this->mRendererId = 0;
}

void VertexBuffer::bind( ) const {
    glBindBuffer( GL_ARRAY_BUFFER, this->mRendererId );
}

void VertexBuffer::unbind( ) const {
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
}