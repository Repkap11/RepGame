#pragma once


class IndexBuffer {
    friend class Renderer;

    unsigned int mRendererId;
    unsigned int count;

  public:
    void init( );
    void set_data( const unsigned int *data, unsigned int count );
    void destroy( );
    void bind( ) const;
    void unbind( ) const;
};