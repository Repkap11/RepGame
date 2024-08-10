#include "common/RepGame.hpp"
#include "common/renderer/index_buffer.hpp"

void IndexBuffer::init( ) {
    this->count = 0;
    glGenBuffers( 1, &this->mRendererId );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, this->mRendererId );
}

void IndexBuffer::set_data( const unsigned int *data, unsigned int count ) {
    this->count = count;
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, this->mRendererId );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, count * sizeof( unsigned int ), data, GL_STATIC_DRAW );
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