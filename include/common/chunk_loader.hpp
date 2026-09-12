#pragma once

#include "chunk.hpp"
#include "renderer/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"
#include "common/utils/terrain_loading_thread.hpp"
#include "common/utils/map_storage.hpp"

class Multiplayer;
class ChunkLoader {
    friend class World;

    TerrainLoadingThread terrain_loading_thread;
    glm::ivec3 chunk_center;
    Chunk *chunkArray;
    Shader shader;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;

    // Compact list of chunks that are loaded and should be rendered.
    // Rebuilt once per frame in render_chunks; iterated by calculate_cull/draw
    // instead of scanning all MAX_LOADED_CHUNKS slots every pass.
    Chunk **drawable_chunks;
    int num_drawable;

    int reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos );
    static inline int process_chunk_position( Chunk &chunk, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next, int force_reload );
    void process_random_ticks( );
    void rebuild_drawable_list( );

  public:
    void init( const glm::vec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords, MapStorage &map_storage );
    void render_chunks( Multiplayer &multiplayer, const glm::vec3 &camera_pos, int limit_render );
    void repopulate_blocks( );
    void calculate_cull( const glm::mat4 &mvp, bool saveAsReflection ) const;
    void draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, bool reflect_only, bool draw_reflect );
    Chunk *get_chunk( const glm::ivec3 &pointed ) const;
    void cleanup( MapStorage &map_storage );
};

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

// The chunk grid is a torus of size (2*R+1)^3. Each chunk's chunk_mod (which is
// invariant for its slot) uniquely maps to an array index, giving O(1) lookup.
static inline int chunk_slot_from_mod( const glm::ivec3 &chunk_mod ) {
    const int sx = 2 * CHUNK_RADIUS_X + 1;
    const int sz = 2 * CHUNK_RADIUS_Z + 1;
    return chunk_mod.x + sx * ( chunk_mod.z + sz * chunk_mod.y );
}

static inline int chunk_slot_from_pos( const glm::ivec3 &chunk_pos ) {
    const int sx = 2 * CHUNK_RADIUS_X + 1;
    const int sy = 2 * CHUNK_RADIUS_Y + 1;
    const int sz = 2 * CHUNK_RADIUS_Z + 1;
    glm::ivec3 cmod;
    cmod.x = ( ( chunk_pos.x % sx ) + sx ) % sx;
    cmod.y = ( ( chunk_pos.y % sy ) + sy ) % sy;
    cmod.z = ( ( chunk_pos.z % sz ) + sz ) % sz;
    return chunk_slot_from_mod( cmod );
}