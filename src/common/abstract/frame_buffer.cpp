#include "common/RepGame.hpp"
#include "common/abstract/frame_buffer.hpp"

void frame_buffer_init( FrameBuffer *frameBuffer ) {
    glGenFramebuffers( 1, &frameBuffer->mRendererId );
    frame_buffer_bind( frameBuffer );
}

void frame_buffer_destroy( const FrameBuffer *frameBuffer ) {
    glDeleteFramebuffers( 1, &frameBuffer->mRendererId );
}

void frame_buffer_bind( const FrameBuffer *frameBuffer ) {
    glBindFramebuffer( GL_FRAMEBUFFER, frameBuffer->mRendererId );
}

void frame_buffer_bind_display( ) {
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void frame_buffer_attach_render_buffer( const FrameBuffer *frameBuffer, const RenderBuffer *renderBuffer ) {
    frame_buffer_bind( frameBuffer );
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderBuffer->mRendererId );
}

void frame_buffer_unbind( const FrameBuffer *frameBuffer ) {
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

bool frame_buffer_ok( const FrameBuffer *frameBuffer ) {
    frame_buffer_bind( frameBuffer );
    return ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
}