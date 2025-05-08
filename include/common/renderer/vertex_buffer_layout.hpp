#pragma once


#define NUM_VERTEX_BUFFER_ELEMENTS 10

typedef struct {
    unsigned int type;
    unsigned int count;
    unsigned char normalized;
} VertexBufferElement;

class VertexBufferLayout {
    friend class VertexArray;
    friend class Chunk;

    unsigned int mRendererId;
    unsigned int current_size;
    unsigned int stride;
    VertexBufferElement elements[ NUM_VERTEX_BUFFER_ELEMENTS ];
    static unsigned int size_of_type( unsigned int type );
    static bool uses_I_attrib( unsigned int type );

  public:
    VertexBufferLayout( );
    static void init_element( VertexBufferElement &vertexBufferElement, unsigned int type, unsigned int count, unsigned int normalized );

    void push_float( unsigned int count );
    void push_unsigned_int( unsigned int count );
    void push_int( unsigned int count );
    void push_unsigned_bytes( unsigned int count );
    void push_float_array( unsigned int count );
    void push_byte( unsigned int count );

    void destroy( );
};