#include "common/RepGame.hpp"
#include "common/abstract/render_buffer.hpp"

void render_buffer_init( RenderBuffer *renderBuffer ) {
    glGenRenderbuffers( 1, &renderBuffer->mRendererId );
    render_buffer_bind( renderBuffer );
}

void render_buffer_set_storage( RenderBuffer *renderBuffer, int width, int height ) {
    render_buffer_bind( renderBuffer );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_RGBA8, width, height );
}

void render_buffer_destroy( const RenderBuffer *renderBuffer ) {
    glDeleteRenderbuffers( 1, &renderBuffer->mRendererId );
}

void render_buffer_bind( const RenderBuffer *renderBuffer ) {
    glBindRenderbuffer( GL_RENDERBUFFER, renderBuffer->mRendererId );
}

void render_buffer_unbind( const RenderBuffer *renderBuffer ) {
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );
}
