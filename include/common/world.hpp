#pragma once

class World;

#include "common/Platforms.hpp"
#include "block.hpp"
#include "chunk_loader.hpp"
#include "common/multiplayer_avatars.hpp"
#include "common/block_debris.hpp"
#include "common/block_bodies.hpp"
#include "common/item_entities.hpp"
#include "common/mobs.hpp"
#include "common/renderer/full_screen_quad.hpp"
#include "common/RenderChain.hpp"

enum WorldDrawQuality { LOW, MEDIUM, HIGH, X_HIGH, LAST };
// Every quality above LOW needs the frame buffer; platforms without one
// (WASM, Android) only cycle through the levels they actually render.
constexpr int WORLD_DRAW_QUALITY_COUNT = SUPPORTS_FRAME_BUFFER ? WorldDrawQuality::LAST : WorldDrawQuality::MEDIUM;
inline WorldDrawQuality world_draw_quality_step( WorldDrawQuality q, int delta ) {
    return static_cast<WorldDrawQuality>( ( ( q + delta ) % WORLD_DRAW_QUALITY_COUNT + WORLD_DRAW_QUALITY_COUNT ) % WORLD_DRAW_QUALITY_COUNT );
}

class World {
    friend class RepGame;
    friend class Multiplayer;
    friend class MobManager;
    friend int test_mobs( );
    friend void test_setup_world( World &world );

    friend int test_lighting( );
    friend int test_falling_blocks( );

    ChunkLoader chunkLoader;
    // Back-pointer to the owning RepGameState's Multiplayer, bound after
    // world.init(): lets an edit into a not-yet-live slot queue itself as a
    // pending diff so it lands at the next load's finalize (the sender's own
    // BLOCK_UPDATE never loops back from the server).
    Multiplayer *multiplayer = nullptr;
    Renderer renderer;
    SkyBox skyBox;
    MultiplayerAvatars multiplayer_avatars;
    BlockDebris blockDebris;
    VertexBufferLayout vbl_block;
    VertexBufferLayout vbl_coords;
    VertexBufferLayout vbl_object_vertex;
    VertexBufferLayout vbl_object_position;
    VertexBufferLayout vbl_debris_instance;
    VertexBufferLayout vbl_primed_tnt_instance;
    VertexBufferLayout vbl_block_object_instance;
    VertexBufferLayout vbl_mob_instance;
    VertexBufferLayout vbl_item_instance;
    Texture reflectionTexture;
    Texture blockTexture;
    Texture fogTexture;
    Texture skyColorTexture;
    Texture depthStencilTexture;
    // Mob skins: one 64x64 layer per MOB_SKIN_*, nearest filtering, no mips
    // (tiny UV islands would bleed). MobManager draws through this array.
    Texture mobsTexture;
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
    Shader block_object_shader;
    Shader mob_shader;
    Shader item_sprite_shader;

    int can_fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset ) const;
    void fixup_chunk( const Chunk &chunk, const glm::ivec3 &offset, const glm::ivec3 &pos, const BlockState &blockState );
    static bool any_neighbor_adj_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );
    static bool any_neighbor_diag_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );
    static bool any_neighbor_grass_id( const Chunk &chunk, const glm::ivec3 &pos, BlockID id );

    bool process_random_ticks_on_chunk( const Chunk &chunk );
    static bool do_random_tick_on_block( const Chunk &chunk, const glm::vec3 &pos, BlockState &blockState );

  public:
    // Client-local mob simulation and rendering (REPGAME_MOBS=0 disables).
    MobManager mobs;
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

    // Furnace containers — same shape as the chest API above. The lit
    // (LIT_FURNACE) and unlit (FURNACE) blocks share one contents entry.
    const FurnaceContents *get_furnace_contents( const glm::ivec3 &block_pos ) const;
    FurnaceContents *get_or_create_furnace_contents( const glm::ivec3 &block_pos );
    // Optimistic per-slot write; false when not a furnace / chunk unloaded.
    bool set_furnace_slot( const glm::ivec3 &block_pos, int slot, const InventorySlot &value );
    // Full-state write (FURNACE_CONTENTS packet / local open); null when not a furnace.
    void set_furnace_contents( const glm::ivec3 &block_pos, const FurnaceContents &contents );
    // Advances every loaded chunk's furnaces one tick (fuel burn, cook
    // progress, output). Singleplayer only — in multiplayer the server is
    // authoritative and pushes state via FURNACE_CONTENTS. Swaps
    // FURNACE<->LIT_FURNACE blocks as furnaces light/extinguish.
    void tick_furnaces( );
    // Spawns break particles if prev_state was a real block that new_id
    // removed. No-op for air, same-id state changes, and liquids.
    void spawn_block_debris( const glm::ivec3 &block_pos, const BlockState &prev_state, BlockID new_id, int num_particles = DEBRIS_PER_BLOCK );
    // CPU physics bodies: falling blocks (sand, gravel, concrete powder,
    // anvils) and primed TNT. ticked from the game loop right after
    // mobs.tick; rendered through two instanced chains in draw.
    BlockBodies bodies;
    // Dropped item entities: client-local physics + pickup; ticked right
    // after bodies, rendered as mini cubes (isometric block items) or
    // extruded sprites.
    ItemEntities items;
    // void overlay_blocks( const glm::ivec3 &block_pos, BlockState *blockStates, int numBlockStates );

    static BlockState get_block_from_chunk( const Chunk &chunk, const glm::ivec3 &block );
    Chunk *get_loaded_chunk( const glm::ivec3 &block ) const;
    void init( const glm::dvec3 &camera_pos, int width, int height, MapStorage &map_storage, int render_radius );
    void change_size( int width, int height );
    void render( Multiplayer &multiplayer, const glm::dvec3 &camera_pos, int limit_render, const glm::mat4 &rotation );
    void draw( const Texture &blocksTexture, const glm::mat4 &mvp, const glm::mat4 &inv_mvp, const glm::mat4 &mvp_reflect, const glm::mat4 &mvp_sky, const glm::mat4 &mvp_sky_reflect, int debug, int draw_mouse_selection, float y_height, bool headInWater,
               WorldDrawQuality worldDrawQuality, const glm::dvec3 &camera_pos, const glm::ivec3 &renderOrigin, float time_s, float daylight, float min_ambient, float alpha );
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