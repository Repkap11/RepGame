#include "common/RepGame.hpp"
#include "common/renderer/frame_buffer.hpp"

// The "display" framebuffer id — 0 (the window backbuffer) in flat mode; a
// per-eye swapchain FBO while a VR view is being drawn (RepGame::draw_frame).
unsigned int FrameBuffer::display_id = 0;

void FrameBuffer::set_display( unsigned int fbo ) {
    display_id = fbo;
}

void FrameBuffer::init( ) {
    glGenFramebuffers( 1, &this->mRendererId );
    // frame_buffer_bind( frameBuffer );
    // frame_buffer_bind_display( );
}

void FrameBuffer::destroy( ) {
    glDeleteFramebuffers( 1, &this->mRendererId );
}

void FrameBuffer::bind( ) const {
    glBindFramebuffer( GL_FRAMEBUFFER, this->mRendererId );
}

void FrameBuffer::bind_display( ) {
    glBindFramebuffer( GL_FRAMEBUFFER, display_id );
}

void FrameBuffer::attach_texture( const Texture &texture, int which_attachment ) const {
    this->bind( );
    showErrors( );
    int attachment;
    if ( texture.format == GL_DEPTH_STENCIL ) {
        attachment = GL_DEPTH_STENCIL_ATTACHMENT;
    } else if ( texture.format == GL_DEPTH_COMPONENT ) {
        attachment = GL_DEPTH_ATTACHMENT;
    } else {
        attachment = GL_COLOR_ATTACHMENT0 + which_attachment;
    }
    glFramebufferTexture2D( GL_FRAMEBUFFER, attachment, texture.target, texture.m_RendererId, 0 );
    // frame_buffer_bind_display( );
    showErrors( );
}

void FrameBuffer::unbind( ) const {
    glBindFramebuffer( GL_FRAMEBUFFER, display_id );
}

bool FrameBuffer::ok( ) const {
    bind( );
    const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
    if ( status != GL_FRAMEBUFFER_COMPLETE ) {
        pr_debug( "Framebuffer status:0x%x", status );
    }
    return ( status == GL_FRAMEBUFFER_COMPLETE );
}