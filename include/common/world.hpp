#pragma once

class World;

#include "block.hpp"
#include "chunk_loader.hpp"
#include "common/multiplayer_avatars.hpp"
#include "common/renderer/full_screen_quad.hpp"
#include "common/RenderChain.hpp"

enum WorldDrawQuality { LOW, MEDIUM, HIGH, X_HIGH, LAST };

class World {
    friend class RepGame;
    friend class Multiplayer;

    ChunkLoader chunkLoader;
    Renderer renderer;
    SkyBox skyBox;
    MultiplayerAvatars multiplayer_avatars;
    VertexBufferLayout vbl_block;
    VertexBufferLayout vbl_coords;
    VertexBufferLayout vbl_object_vertex;
    VertexBufferLayout vbl_object_position;
    Texture reflectionTexture;
    Texture blockTexture;
    Texture depthStencilTexture;
    FrameBuffer frameBuffer;
    FullScreenQuad fullScreenQuad;
    MouseSelection mouseSelection;
    Shader object_shader;

    int can_fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset ) const;
    void fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset, const glm::ivec3 &pos, const BlockState &blockState ) const;
    static bool any_neighbor_adj_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );
    static bool any_neighbor_diag_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );
    static bool any_neighbor_grass_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );

    bool process_random_ticks_on_chunk( const Chunk &chunk ) const;
    static bool do_random_tick_on_block( const Chunk &chunk, const glm::vec3 &pos, BlockState &blockState );

  public:
    BlockState get_loaded_block( const glm::ivec3 &block_pos ) const;

    void set_loaded_block( const glm::ivec3 &block_pos, BlockState blockState ) const;
    // void overlay_blocks( const glm::ivec3 &block_pos, BlockState *blockStates, int numBlockStates );

    static BlockState get_block_from_chunk( const Chunk &chunk, const glm::ivec3 &block );
    Chunk *get_loaded_chunk( const glm::ivec3 &block ) const;
    void init( const glm::vec3 &camera_pos, int width, int height, MapStorage &map_storage );
    void change_size( int width, int height );
    void render( Multiplayer &multiplayer, const glm::vec3 &camera_pos, int limit_render, const glm::mat4 &rotation );
    void draw( const Texture &blocksTexture, const glm::mat4 &mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater,
               WorldDrawQuality worldDrawQuality );
    void process_random_ticks( ) const;
    void set_selected_block( const glm::ivec3 &selected, bool shouldDraw );
    void cleanup( MapStorage &map_storage );
};