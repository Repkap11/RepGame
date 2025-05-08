#include "common/RepGame.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"

VertexBufferLayout::VertexBufferLayout( ) : mRendererId( 0 ), current_size(0 ), stride(0 ), elements{ } {
}

unsigned int VertexBufferLayout::size_of_type( unsigned int type ) {
    switch ( type ) {
        case GL_FLOAT:
            return 4;
        case GL_UNSIGNED_INT:
        case GL_INT:
            return 4;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        default:
            pr_debug( "Error, unexpected type" );
            return 0;
    }
}

bool VertexBufferLayout::uses_I_attrib( unsigned int type ) {
    switch ( type ) {
        case GL_FLOAT:
            return false;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return true;
        default:
            pr_debug( "Error, unexpected type" );
            return 1;
    }
}

inline void VertexBufferLayout::init_element( VertexBufferElement &vertexBufferElement, const unsigned int type, const unsigned int count, const unsigned int normalized ) {
    vertexBufferElement.type = type;
    vertexBufferElement.count = count;
    vertexBufferElement.normalized = normalized;
}

void VertexBufferLayout::push_float( unsigned int count ) {
    if ( this->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        VertexBufferLayout::init_element(         //
            this->elements[ this->current_size ], //
            GL_FLOAT,                             //
            count,                                //
            GL_FALSE );
        this->current_size += 1;
        this->stride += count * VertexBufferLayout::size_of_type( GL_FLOAT );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void VertexBufferLayout::push_float_array( const unsigned int count ) {
    for ( unsigned int i = 0; i < count; i++ ) {
        VertexBufferLayout::push_float( 1 );
    }
}

void VertexBufferLayout::push_unsigned_int( const unsigned int count ) {
    if ( this->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        VertexBufferLayout::init_element(         //
            this->elements[ this->current_size ], //
            GL_UNSIGNED_INT,                      //
            count,                                //
            GL_TRUE );
        this->current_size += 1;
        this->stride += count * VertexBufferLayout::size_of_type( GL_UNSIGNED_INT );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void VertexBufferLayout::push_byte( unsigned int count ) {
    if ( this->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        VertexBufferLayout::init_element(         //
            this->elements[ this->current_size ], //
            GL_UNSIGNED_BYTE,                     //
            count,                                //
            GL_FALSE );
        this->current_size += 1;
        this->stride += count * VertexBufferLayout::size_of_type( GL_BYTE );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void VertexBufferLayout::push_int( unsigned int count ) {
    if ( this->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        VertexBufferLayout::init_element(         //
            this->elements[ this->current_size ], //
            GL_INT,                               //
            count,                                //
            GL_FALSE );
        this->current_size += 1;
        this->stride += count * VertexBufferLayout::size_of_type( GL_INT );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void VertexBufferLayout::push_unsigned_bytes( unsigned int count ) {
    if ( this->current_size < NUM_VERTEX_BUFFER_ELEMENTS ) {
        VertexBufferLayout::init_element(         //
            this->elements[ this->current_size ], //
            GL_UNSIGNED_BYTE,                     //
            count,                                //
            GL_FALSE );
        this->current_size += 1;
        this->stride += count * VertexBufferLayout::size_of_type( GL_UNSIGNED_BYTE );
    } else {
        pr_debug( "Error, too meny elements in VertexBufferLayout, just increase the hardcoded limit" );
    }
}

void VertexBufferLayout::destroy( ) {
}
