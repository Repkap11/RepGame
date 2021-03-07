#include "common/RepGame.hpp"
#include "common/abstract/frame_buffer.hpp"

void frame_buffer_init( FrameBuffer *frameBuffer ) {
    // GLint maxColAttchments = 0;
    // glGetIntegerv( GL_MAX_COLOR_ATTACHMENTS, &maxColAttchments );
    // pr_debug( "Max attachments:%d", maxColAttchments );
    glGenFramebuffers( 1, &frameBuffer->mRendererId );
    texture_init_empty_depth_stencil( &frameBuffer->depthStencilTexture, 0 );
    frame_buffer_bind( frameBuffer );
    showErrors( );
    // pr_debug( "Value:%d", frameBuffer->depthStencilTexture.m_RendererId );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, frameBuffer->depthStencilTexture.target, frameBuffer->depthStencilTexture.m_RendererId, 0 );
    showErrors( );
    frame_buffer_bind_display( );
    showErrors( );
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

void frame_buffer_change_size( FrameBuffer *frameBuffer, int width, int height ) {
    texture_change_size( &frameBuffer->depthStencilTexture, width, height );
    showErrors( );
}

void frame_buffer_attach_texture( FrameBuffer *frameBuffer, Texture *texture, int which_attachment ) {
    frame_buffer_bind( frameBuffer );
    showErrors( );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + which_attachment, texture->target, texture->m_RendererId, 0 );
    showErrors( );
}

void frame_buffer_unbind( const FrameBuffer *frameBuffer ) {
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

bool frame_buffer_ok( const FrameBuffer *frameBuffer ) {
    frame_buffer_bind( frameBuffer );
    return ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
}