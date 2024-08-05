#ifndef HEADER_VERTEX_BUFFER_H
#define HEADER_VERTEX_BUFFER_H

class VertexBuffer {
    unsigned int mRendererId;
    unsigned int size;

  public:
    void init( );
    void destroy( );
    void bind( ) const;
    void unbind( ) const;
    void set_data( const void *data, unsigned int size );
    void set_subdata( const void *data, unsigned int offset, unsigned int size );
};

#endif