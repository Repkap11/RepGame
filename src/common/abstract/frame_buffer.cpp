#include "common/RepGame.hpp"
#include "common/abstract/frame_buffer.hpp"

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
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void FrameBuffer::attach_texture( const Texture &texture, int which_attachment ) const {
    this->bind( );
    showErrors( );
    int attachment;
    if ( texture.format == GL_DEPTH_STENCIL ) {
        attachment = GL_DEPTH_STENCIL_ATTACHMENT;
    } else {
        attachment = GL_COLOR_ATTACHMENT0 + which_attachment;
    }
    glFramebufferTexture2D( GL_FRAMEBUFFER, attachment, texture.target, texture.m_RendererId, 0 );
    // frame_buffer_bind_display( );
    showErrors( );
}

void FrameBuffer::unbind( ) const {
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

bool FrameBuffer::ok( ) const {
    bind( );
    return ( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
}