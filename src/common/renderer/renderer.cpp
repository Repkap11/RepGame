#include "common/RepGame.hpp"
#include "common/renderer/renderer.hpp"

void Renderer::draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances ) const {
    vertexArray.bind( );
    indexBuffer.bind( );
    shader.bind( );
    glDrawElementsInstanced( GL_TRIANGLES, indexBuffer.count, GL_UNSIGNED_INT, NULL, num_instances );

    // pr_debug("Drawing a chunk");
}

void Renderer::draw( const VertexArray &vertexArray, const IndexBuffer &indexBuffer, const Shader &shader, unsigned int num_instances,
                     const unsigned int index_count, const uintptr_t index_offset ) const {
    vertexArray.bind( );
    indexBuffer.bind( );
    shader.bind( );
    glDrawElementsInstanced( GL_TRIANGLES, index_count, GL_UNSIGNED_INT, reinterpret_cast<const void *>( index_offset ), num_instances );
}
