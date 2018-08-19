#include "RepGame.h"
#include "abstract/renderer.h"

void renderer_draw( const Renderer *renderer, const VertexArray *vertexArray, const IndexBuffer *indexBuffer, const Shader *shader, unsigned int num_instances ) {
    vertex_array_bind( vertexArray );
    index_buffer_bind( indexBuffer );
    shader_bind( shader );
    glPatchParameteri( GL_PATCH_VERTICES, 3 );
    // float patch_levels[ 4 ] = {2, 2, 2, 2};
    // glPatchParameterfv( GL_PATCH_DEFAULT_OUTER_LEVEL, patch_levels );
    // glPatchParameterfv( GL_PATCH_DEFAULT_INNER_LEVEL, patch_levels );
    // glDrawElements( GL_TRIANGLES, indexBuffer->count, GL_UNSIGNED_INT, NULL );
    // glDrawElements( GL_PATCHES, indexBuffer->count, GL_UNSIGNED_INT, NULL );
    // glDrawElementsInstanced( GL_TRIANGLES, indexBuffer->count, GL_UNSIGNED_INT, NULL, num_instances );
    glDrawElementsInstanced( GL_PATCHES, indexBuffer->count, GL_UNSIGNED_INT, NULL, num_instances );
    showErrors( );

    // pr_debug("Drawing a chunk");
}
