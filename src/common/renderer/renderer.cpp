#include "common/RepGame.hpp"
#include "common/renderer/renderer.hpp"

void Renderer::draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const {
    vertexArray.bind( );
    indexBuffer.bind( );
    shader.bind( );
    glDrawElementsInstanced( GL_TRIANGLES, indexBuffer.count, GL_UNSIGNED_INT, NULL, num_instances );

    // pr_debug("Drawing a chunk");
}
