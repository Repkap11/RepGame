#define GL_GLEXT_PROTOTYPES
#include "RepGame.h"
#include "abstract/vertex_buffer_layout.h"

void vertex_buffer_layout_init( VertexBufferLayout *vertexBufferLayout, const void *data ) {
    vertexBufferLayout->current_size = 0;
    vertexBufferLayout->stride = 0;
}

void vertex_buffer_layout_destroy( const VertexBufferLayout *vertexBufferLayout ) {
}

int vertex_buffer_layout_size_of_type( unsigned int type ) {
    switch ( type ) {
        case GL_FLOAT:
            return 4;
        case GL_UNSIGNED_INT:
            return 4;
        default:
            pr_debug( "Error, unexpected type" );
            return 0;
    }
}

void vertex_buffer_layout_init_element( VertexBufferElement *vertexBufferElement, unsigned int type, unsigned int count, unsigned int normalized ) {
    vertexBufferElement->type = type;
    vertexBufferElement->count = count;
    vertexBufferElement->normalized = normalized;
}

void vertex_buffer_layout_push_float( VertexBufferLayout *vertexBufferLayout, int count ) {
    if ( vertexBufferLayout->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        vertex_buffer_layout_init_element(                                     //
            &vertexBufferLayout->elements[ vertexBufferLayout->current_size ], //
            GL_FLOAT,                                                          //
            count,                                                             //
            GL_FALSE );
        vertexBufferLayout->current_size += 1;
        vertexBufferLayout->stride += count * vertex_buffer_layout_size_of_type( GL_FLOAT );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void vertex_buffer_layout_push_unsigned_int( VertexBufferLayout *vertexBufferLayout, int count ) {
    if ( vertexBufferLayout->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        vertex_buffer_layout_init_element(                                     //
            &vertexBufferLayout->elements[ vertexBufferLayout->current_size ], //
            GL_UNSIGNED_INT,                                                   //
            count,                                                             //
            GL_FALSE );
        vertexBufferLayout->current_size += 1;
        vertexBufferLayout->stride += count * vertex_buffer_layout_size_of_type( GL_UNSIGNED_INT );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void vertex_buffer_layout_bind( const VertexBufferLayout *vertexBufferLayout ) {
}

void vertex_buffer_layout_unbind( const VertexBufferLayout *vertexBufferLayout ) {
}