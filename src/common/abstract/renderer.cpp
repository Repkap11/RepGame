#include "common/RepGame.hpp"
#include "common/abstract/renderer.hpp"

void Renderer::draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const {
    vertexArray.bind( );
    indexBuffer.bind( );
    shader.bind( );
    showErrors( );
    glDrawElementsInstanced( GL_TRIANGLES, indexBuffer.count, GL_UNSIGNED_INT, NULL, num_instances );
    showErrors( );

    // pr_debug("Drawing a chunk");
}

void Renderer::draw_lines( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const {
    vertexArray.bind( );
    indexBuffer.bind( );
    shader.bind( );
    glLineWidth( 3 );
    glDrawElementsInstanced( GL_LINES, indexBuffer.count, GL_UNSIGNED_INT, NULL, num_instances );
    showErrors( );

    // pr_debug("Drawing a chunk");
}
