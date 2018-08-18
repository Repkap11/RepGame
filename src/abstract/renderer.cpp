#include "RepGame.h"
#include "abstract/renderer.h"

void renderer_draw( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader, unsigned int num_instances ) {
    vertex_array_bind( vertexArray );
    index_buffer_bind( indexBuffer );
    shader_bind( shader );
    glPatchParameteri( GL_PATCH_VERTICES, 3 );
    // glDrawElements( GL_TRIANGLES, indexBuffer->count, GL_UNSIGNED_INT, NULL );
    glDrawElementsInstanced( GL_PATCHES, indexBuffer->count, GL_UNSIGNED_INT, NULL, num_instances );
    showErrors( );

    // pr_debug("Drawing a chunk");
}
