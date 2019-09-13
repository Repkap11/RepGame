#include "common/RepGame.hpp"

#include "common/abstract/vertex_array.hpp"
#include "common/abstract/vertex_buffer.hpp"
#include "common/abstract/vertex_buffer_layout.hpp"

void vertex_array_init( VertexArray *vertexArray ) {
    glGenVertexArrays( 1, &vertexArray->mRendererId );
    vertex_array_bind( vertexArray );
    // int n = 0;
    // glGetIntegerv( GL_MAX_VERTEX_ATTRIBS, &n );
    // pr_debug( "GL_MAX_VERTEX_ATTRIBS:%d", n );
}

void vertex_array_destroy( VertexArray *vertexArray ) {
    glDeleteVertexArrays( 1, &vertexArray->mRendererId );
    vertexArray->mRendererId = 0;
}

void vertex_array_add_buffer( VertexArray *vertexArray, const VertexBuffer *vertexBuffer, const VertexBufferLayout *vertexBufferLayout, unsigned int divisor, unsigned int stride ) {
    vertex_array_bind( vertexArray );
    vertex_buffer_bind( vertexBuffer );
    unsigned int offset = 0;
    const unsigned int current_size = vertexBufferLayout->current_size;
    // pr_debug( "Current Size:%u", current_size );
    for ( unsigned int i = 0; i < current_size; i++ ) {
        // keep track of last i inside of vertexArray so next time we can increment it
        // pr_debug( "Binding VertexBufferElement:%d", i + divisor * 3 );
        int index = i + divisor * stride;
        const VertexBufferElement *element = &vertexBufferLayout->elements[ i ];
        glEnableVertexAttribArray( index );
        glVertexAttribDivisor( index, divisor );
// Since we have a vertex array bound, offset "specifies an offset into the array in the data store of that buffer"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
        const void *offset_pointer = ( const void * )offset;
#pragma GCC diagnostic pop
        // pr_debug( "Adding attrib:%d", index );
        if ( vertex_buffer_layout_uses_I_attrib( element->type ) ) {
            glVertexAttribIPointer( index, element->count, element->type, vertexBufferLayout->stride, offset_pointer );
        } else {
            glVertexAttribPointer( index, element->count, element->type, element->normalized, vertexBufferLayout->stride, offset_pointer );
        }
        offset += element->count * vertex_buffer_layout_size_of_type( element->type );
    }
}

void vertex_array_bind( const VertexArray *vertexArray ) {
    glBindVertexArray( vertexArray->mRendererId );
}

void vertex_array_unbind( const VertexArray *vertexArray ) {
    glBindVertexArray( 0 );
}