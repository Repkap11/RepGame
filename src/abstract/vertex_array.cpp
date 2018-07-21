#include "RepGame.h"

#include "abstract/vertex_array.h"
#include "abstract/vertex_buffer.h"
#include "abstract/vertex_buffer_layout.h"

void vertex_array_init( VertexArray *vertexArray ) {
    glGenVertexArrays( 1, &vertexArray->mRendererId );
    vertex_array_bind( vertexArray );
}

void vertex_array_destroy( VertexArray *vertexArray ) {
    glDeleteVertexArrays( 1, &vertexArray->mRendererId );
    vertexArray->mRendererId = 0;
}

void vertex_array_add_buffer( VertexArray *vertexArray, const VertexBuffer *vertexBuffer, const VertexBufferLayout *vertexBufferLayout, unsigned int divisor ) {
    vertex_array_bind( vertexArray );
    vertex_buffer_bind( vertexBuffer );
    unsigned int offset = 0;
    const unsigned int current_size = vertexBufferLayout->current_size;
    for ( unsigned int i = 0; i < current_size; i++ ) {
        // TODO keep track of last i inside of vertexArray so next time we can increment it
        // pr_debug( "Binding VertexBufferElement:%d", i + divisor * 3 );
        const VertexBufferElement *element = &vertexBufferLayout->elements[ i ];
        glEnableVertexAttribArray( i + divisor * 3 );
        glVertexAttribDivisor( i + divisor * 3, divisor );
        glVertexAttribPointer( i + divisor * 3, element->count, element->type, element->normalized, vertexBufferLayout->stride, ( const void * )offset );
        offset += element->count * vertex_buffer_layout_size_of_type( element->type );
    }
}

void vertex_array_bind( const VertexArray *vertexArray ) {
    glBindVertexArray( vertexArray->mRendererId );
}

void vertex_array_unbind( const VertexArray *vertexArray ) {
    glBindVertexArray( 0 );
}