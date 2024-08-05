#include "common/RepGame.hpp"

#include "common/abstract/vertex_array.hpp"
#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"

void VertexArray::init( ) {
    glGenVertexArrays( 1, &this->mRendererId );
    this->bind( );
    this->stride = 0;
    this->divisor = 0;
    // int n = 0;
    // glGetIntegerv( GL_MAX_VERTEX_ATTRIBS, &n );
    // pr_debug( "GL_MAX_VERTEX_ATTRIBS:%d", n );
}

void VertexArray::destroy( ) {
    glDeleteVertexArrays( 1, &this->mRendererId );
    this->mRendererId = 0;
}

void VertexArray::add_buffer( const VertexBuffer &vertexBuffer, const VertexBufferLayout &vertexBufferLayout ) {
    this->bind( );
    vertexBuffer.bind( );
    unsigned int offset = 0;
    const unsigned int current_size = vertexBufferLayout.current_size;
    // pr_debug( "Current Size:%u", current_size );
    for ( unsigned int i = 0; i < current_size; i++ ) {
        // keep track of last i inside of vertexArray so next time we can increment it
        // pr_debug( "Binding VertexBufferElement:%d", i + divisor * 3 );
        int index = i + this->divisor * this->stride;
        const VertexBufferElement *element = &vertexBufferLayout.elements[ i ];
        glEnableVertexAttribArray( index );
        glVertexAttribDivisor( index, divisor );
// Since we have a vertex array bound, offset "specifies an offset into the array in the data store of that buffer"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
        const void *offset_pointer = ( const void * )offset;
#pragma GCC diagnostic pop
        // pr_debug( "Adding attrib:%d", index );
        if ( VertexBufferLayout::uses_I_attrib( element->type ) ) {
            glVertexAttribIPointer( index, element->count, element->type, vertexBufferLayout.stride, offset_pointer );
        } else {
            glVertexAttribPointer( index, element->count, element->type, element->normalized, vertexBufferLayout.stride, offset_pointer );
        }
        offset += element->count * VertexBufferLayout::size_of_type( element->type );
    }
    this->divisor += 1;
    this->stride += current_size;
}

void VertexArray::bind( ) const {
    glBindVertexArray( this->mRendererId );
}

void VertexArray::unbind( ) const {
    glBindVertexArray( 0 );
}