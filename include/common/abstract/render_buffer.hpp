#ifndef HEADER_RENDER_BUFFER_H
#define HEADER_RENDER_BUFFER_H

typedef struct {
    unsigned int mRendererId;
} RenderBuffer;

void render_buffer_init( RenderBuffer *renderBuffer );
void render_buffer_set_storage( RenderBuffer *renderBuffer, int width, int height );
void render_buffer_destroy( const RenderBuffer *renderBuffer );
void render_buffer_bind( const RenderBuffer *renderBuffer );
void render_buffer_bind_display( );
void render_buffer_unbind( const RenderBuffer *renderBuffer );
bool render_buffer_ok( );

#endif