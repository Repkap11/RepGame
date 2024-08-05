#ifndef HEADER_FRAME_BUFFER_H
#define HEADER_FRAME_BUFFER_H

class FrameBuffer {
    unsigned int mRendererId;

  public:
    void init( );
    void destroy( );
    void bind( ) const;
    static void bind_display( );
    void attach_texture( const Texture &texture, int which_attachment ) const;
    void unbind( ) const;
    bool ok( ) const;

};

#endif