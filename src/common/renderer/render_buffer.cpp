#include "common/RepGame.hpp"
#include "common/renderer/render_buffer.hpp"

void RenderBuffer::init( ) {
    glGenRenderbuffers( 1, &this->mRendererId );
    RenderBuffer::bind( );
}

void RenderBuffer::set_storage( int width, int height ) {
    RenderBuffer::bind( );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA8, width, height );
}

void RenderBuffer::destroy( ) {
    glDeleteRenderbuffers( 1, &this->mRendererId );
    this->mRendererId = 0;
}

void RenderBuffer::bind( ) const {
    glBindRenderbuffer( GL_RENDERBUFFER, this->mRendererId );
}

void RenderBuffer::unbind( ) const {
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );
}