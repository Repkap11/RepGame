#pragma once

class World;

#include "block.hpp"
#include "chunk_loader.hpp"
#include "common/multiplayer_avatars.hpp"
#include "common/block_debris.hpp"
#include "common/primed_tnt.hpp"
#include "common/renderer/full_screen_quad.hpp"
#include "common/RenderChain.hpp"

enum WorldDrawQuality { LOW, MEDIUM, HIGH, X_HIGH, LAST };

class World {
    friend class RepGame;
    friend class Multiplayer;
    friend void test_setup_world( World &world );

    friend int test_lighting( );

    ChunkLoader chunkLoader;
    Renderer renderer;
    SkyBox skyBox;
    MultiplayerAvatars multiplayer_avatars;
    BlockDebris blockDebris;
    PrimedTnt primedTnt;
    VertexBufferLayout vbl_block;
    VertexBufferLayout vbl_coords;
    VertexBufferLayout vbl_object_vertex;
    VertexBufferLayout vbl_object_position;
    VertexBufferLayout vbl_debris_instance;
    VertexBufferLayout vbl_primed_tnt_instance;
    Texture reflectionTexture;
    Texture blockTexture;
    Texture fogTexture;
    Texture skyColorTexture;
    Texture depthStencilTexture;
    // Copy of the scene depth used by the water shader for screen-space
    // reflections. Filled by a depth blit into depthFrameBuffer after the
    // scene pass (multisample depth-stencil views and split depth/stencil
    // attachments are unsupported on some drivers).
    Texture depthTexture;
    FrameBuffer frameBuffer;
    FrameBuffer depthFrameBuffer;
    int fboWidth = 0;
    int fboHeight = 0;
    FullScreenQuad fullScreenQuad;
    MouseSelection mouseSelection;
    Shader object_shader;
    Shader debris_shader;
    Shader primed_tnt_shader;

    int can_fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset ) const;
    void fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset, const glm::ivec3 &pos, const BlockState &blockState );
    static bool any_neighbor_adj_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );
    static bool any_neighbor_diag_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );
    static bool any_neighbor_grass_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );

    bool process_random_ticks_on_chunk( const Chunk &chunk );
    static bool do_random_tick_on_block( const Chunk &chunk, const glm::vec3 &pos, BlockState &blockState );

  public:
    BlockState get_loaded_block( const glm::ivec3 &block_pos ) const;

    // Non-const: block edits seed the light propagation queues on chunkLoader.
    void set_loaded_block( const glm::ivec3 &block_pos, BlockState blockState );

    // Chest containers. Contents for the chest at block_pos, or nullptr when
    // the block isn't a chest, its chunk isn't loaded, or no contents exist.
    const ChestContents *get_chest_contents( const glm::ivec3 &block_pos ) const;
    // Same, but creates an empty entry when the block is a chest with none.
    ChestContents *get_or_create_chest_contents( const glm::ivec3 &block_pos );
    // Optimistic per-slot write; false when not a chest / chunk unloaded.
    bool set_chest_slot( const glm::ivec3 &block_pos, int slot, const InventorySlot &value );
    // Full-state write (CHEST_CONTENTS packet / local open); null when not a chest.
    void set_chest_contents( const glm::ivec3 &block_pos, const ChestContents &contents );
    // Spawns break particles if prev_state was a real block that new_id
    // removed. No-op for air, same-id state changes, and liquids.
    void spawn_block_debris( const glm::ivec3 &block_pos, const BlockState &prev_state, BlockID new_id, int num_particles = DEBRIS_PER_BLOCK );
    // Spawns a primed (flying + flashing) TNT entity. center is the
    // world-space cube center; ground_t is the predicted terrain-impact time
    // clamping the shader's ballistic flight (>= life_s for free flight).
    // Returns the entity so its detonation can despawn it.
    entt::entity spawn_primed_tnt( const glm::vec3 &center, const glm::vec3 &velocity, float life_s, float ground_t );
    // Removes a live primed TNT entity (the blast it was fusing for fired).
    void despawn_primed_tnt( entt::entity entity );
    // void overlay_blocks( const glm::ivec3 &block_pos, BlockState *blockStates, int numBlockStates );

    static BlockState get_block_from_chunk( const Chunk &chunk, const glm::ivec3 &block );
    Chunk *get_loaded_chunk( const glm::ivec3 &block ) const;
    void init( const glm::dvec3 &camera_pos, int width, int height, MapStorage &map_storage );
    void change_size( int width, int height );
    void render( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, int limit_render, const glm::mat4 &rotation );
    void draw( const Texture &blocksTexture, const glm::mat4 &mvp, const glm::mat4 &inv_mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater,
               WorldDrawQuality worldDrawQuality, const glm::dvec3 &camera_pos, const glm::ivec3 &renderOrigin, float time_s, float daylight, float min_ambient );
    void process_random_ticks( );
    void set_selected_block( const glm::ivec3 &selected, bool shouldDraw );
    void screenshot( const std::string &prefix ) const;
    // Binds u_DebugFlags on the chunk shader: REPGAME_CAPTURE per-view flags,
    // plus bit 32 in normal play when the mipmapped light volume is off.
    void set_chunk_debug_flags( int flags );
    void cleanup( MapStorage &map_storage );
    int get_num_drawable_chunks( ) const { return chunkLoader.num_drawable; }
    int get_num_remeshed_chunks( ) const { return chunkLoader.num_remeshed_this_frame; }
};