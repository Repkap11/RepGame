#include "common/RepGame.hpp"
#include "common/abstract/frame_buffer.hpp"

void frame_buffer_init( FrameBuffer *frameBuffer ) {
    glGenFramebuffers( 1, &frameBuffer->mRendererId );
    // frame_buffer_bind( frameBuffer );
    // frame_buffer_bind_display( );
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

void frame_buffer_attach_texture( FrameBuffer *frameBuffer, Texture *texture, int which_attachment ) {
    frame_buffer_bind( frameBuffer );
    showErrors( );
    int attachment;
    if ( texture->format == GL_DEPTH_STENCIL ) {
        attachment = GL_DEPTH_STENCIL_ATTACHMENT;
    } else {
        attachment = GL_COLOR_ATTACHMENT0 + which_attachment;
    }
    glFramebufferTexture2D( GL_FRAMEBUFFER, attachment, texture->target, texture->m_RendererId, 0 );
    // frame_buffer_bind_display( );
    showErrors( );
}

void frame_buffer_unbind( const FrameBuffer *frameBuffer ) {
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

bool frame_buffer_ok( const FrameBuffer *frameBuffer ) {
    frame_buffer_bind( frameBuffer );
    return ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
}