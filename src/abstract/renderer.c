#define GL_GLEXT_PROTOTYPES
#include <GL/glew.h>
#include "abstract/renderer.h"

void renderer_draw( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader ) {
    vertex_array_bind( vertexArray );
    index_buffer_bind( indexBuffer );
    // shader_bind( &shader );
    glDrawElements( GL_TRIANGLES, indexBuffer->count, GL_UNSIGNED_INT, NULL );
}

void renderer_clear( const Renderer *renderer ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}