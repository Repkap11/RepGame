#include "RepGame.hpp"
#include "abstract/index_buffer.hpp"

void index_buffer_init( IndexBuffer *indexBuffer ) {
    indexBuffer->count = 0;
    glGenBuffers( 1, &indexBuffer->mRendererId );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer->mRendererId );
}

void index_buffer_set_data( IndexBuffer *indexBuffer, unsigned int *data, unsigned int count ) {
    indexBuffer->count = count;
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer->mRendererId );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, count * sizeof( unsigned int ), data, GL_STATIC_DRAW );
}

void index_buffer_destroy( const IndexBuffer *indexBuffer ) {
    glDeleteBuffers( 1, &indexBuffer->mRendererId );
}

void index_buffer_bind( const IndexBuffer *indexBuffer ) {
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indexBuffer->mRendererId );
}

void index_buffer_unbind( const IndexBuffer *indexBuffer ) {
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}