#pragma once

#include "common/block.hpp"
#include "common/RenderChain.hpp"
#include "common/renderer/shader.hpp"

class World;

class MouseSelection {
    // One instance per possible sub-box so multi-box models (stairs, fences)
    // draw a shaped outline; extras sit collapsed at zero scale.
    // FENCE_MAX_BOXES (5) also covers STAIRS_MAX_BOXES (3).
    entt::entity entity[ FENCE_MAX_BOXES ];
    int shouldDraw;
    BlockState selected_block_state;
    float break_progress;
    RenderChain<CubeFace, BlockCoords> render_chain_mouse_selection;
    Shader shader;

  public:
    void init( const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void set_block( const World &world, const glm::ivec3 &pos, bool shouldDraw, const BlockState &blockState );
    // Stop drawing the outline (selected block unloaded/gone).
    void hide( );
    // 0 = no damage overlay, 1 = fully cracked (about to break).
    void set_break_progress( float progress );
    void draw( const Renderer &renderer, const glm::mat4 &mvp, const glm::vec3 &origin );
    void destroy( );
};
