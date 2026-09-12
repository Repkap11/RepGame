#include "common/RepGame.hpp"
#include "common/renderer/index_buffer.hpp"

void IndexBuffer::init( ) {
    this->count = 0;
    this->size = 0;
    glGenBuffers( 1, &this->mRendererId );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, this->mRendererId );
}

void IndexBuffer::set_data( const unsigned int *data, unsigned int count ) {
    this->count = count;
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, this->mRendererId );
    const GLsizeiptr byte_size = static_cast<GLsizeiptr>( count ) * sizeof( unsigned int );
    if ( byte_size == this->size ) {
        // Size unchanged: update in place instead of reallocating.
        if ( byte_size > 0 ) {
            glBufferSubData( GL_ELEMENT_ARRAY_BUFFER, 0, byte_size, data );
        }
    } else {
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, byte_size, data, GL_STATIC_DRAW );
        this->size = byte_size;
    }
}

void IndexBuffer::destroy( ) {
    glDeleteBuffers( 1, &this->mRendererId );
    this->mRendererId = 0;
}

void IndexBuffer::bind( ) const {
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, this->mRendererId );
}

void IndexBuffer::unbind( ) const {
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}