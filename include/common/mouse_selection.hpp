#pragma once

#include "common/RenderChain.hpp"
#include "common/renderer/shader.hpp"

class MouseSelection {
    entt::entity entity;
    int shouldDraw;
    BlockState selected_block_state;
    RenderChain<CubeFace, BlockCoords> render_chain_mouse_selection;
    Shader shader;

  public:
    void init( const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void set_block( const glm::ivec3 &pos, bool shouldDraw, const BlockState &blockState );
    void draw( const Renderer &renderer, const glm::mat4 &mvp, const glm::vec3 &origin );
    void destroy( );
};
