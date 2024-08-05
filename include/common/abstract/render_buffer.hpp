#ifndef HEADER_RENDER_BUFFER_H
#define HEADER_RENDER_BUFFER_H

class RenderBuffer {
    unsigned int mRendererId;

  public:
    void init( );
    void set_storage( int width, int height );
    void destroy( );
    void bind( ) const;
    void bind_display( );
    void unbind( ) const;
    bool ok( );
};

#endif