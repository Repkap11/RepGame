#pragma once

class FrameBuffer {
    unsigned int mRendererId;
    // What bind_display()/unbind() consider the display: 0 (window
    // backbuffer) flat, or the current eye's swapchain FBO in VR.
    static unsigned int display_id;

  public:
    void init( );
    void destroy( );
    void bind( ) const;
    static void set_display( unsigned int fbo );
    static void bind_display( );
    void attach_texture( const Texture &texture, int which_attachment ) const;
    void unbind( ) const;
    bool ok( ) const;
    unsigned int id( ) const { return mRendererId; }

};