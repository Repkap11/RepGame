#ifndef HEADER_FRAME_BUFFER_H
#define HEADER_FRAME_BUFFER_H

typedef struct {
    unsigned int mRendererId;
} FrameBuffer;

void frame_buffer_init( FrameBuffer *frameBuffer );
void frame_buffer_destroy( const FrameBuffer *frameBuffer );
void frame_buffer_bind( const FrameBuffer *frameBuffer );
void frame_buffer_bind_display( );
// void frame_buffer_attach_render_buffer( const FrameBuffer *frameBuffer, const RenderBuffer *renderBuffer );
void frame_buffer_attach_texture( FrameBuffer *frameBuffer, Texture *texture );
void frame_buffer_unbind( const FrameBuffer *frameBuffer );
bool frame_buffer_ok( const FrameBuffer *frameBuffer );

#endif