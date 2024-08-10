#pragma once


class RenderBuffer {
    unsigned int mRendererId;

  public:
    void init( );
    void set_storage( int width, int height );
    void destroy( );
    void bind( ) const;
    void unbind( ) const;
};