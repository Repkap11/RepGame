#pragma once


class VertexBuffer {
    unsigned int mRendererId;
    unsigned int size = 0;

  public:
    void init( );
    void destroy( );
    void bind( ) const;
    void unbind( ) const;
    void set_data( const void *data, unsigned int size );
    void set_subdata( const void *data, unsigned int offset, unsigned int size );
    // Sizes the buffer store without uploading anything — contents are
    // undefined until filled via set_subdata.
    void reserve( unsigned int size );
};