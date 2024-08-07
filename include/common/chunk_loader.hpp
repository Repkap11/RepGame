#ifndef HEADER_CHUNK_LOADER_H
#define HEADER_CHUNK_LOADER_H
#include "chunk.hpp"
#include "abstract/renderer.hpp"
#include "mouse_selection.hpp"
#include "sky_box.hpp"

class ChunkLoader {
    friend class World;
    
    glm::ivec3 chunk_center;
    Chunk *chunkArray;
    Shader shader;
    struct {
        VertexBuffer vb_block;
    } solid;
    struct {
        VertexBuffer vb_block;
    } water;

    static inline int reload_if_out_of_bounds( Chunk &chunk, const glm::ivec3 &chunk_pos );
    static inline int process_chunk_position( Chunk &chunk, const glm::ivec3 &chunk_diff, const glm::ivec3 &center_previous, const glm::ivec3 &center_next, int force_reload );
    void process_random_ticks( );

  public:
    void init( const glm::vec3 &camera_pos, const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void render_chunks( const glm::vec3 &camera_pos, int limit_render );
    void repopulate_blocks( );
    void calculate_cull( const glm::mat4 &mvp, bool saveAsReflection );
    void draw( const glm::mat4 &mvp, const Renderer &renderer, const Texture &texture, bool reflect_only, bool draw_reflect );
    Chunk *get_chunk( const glm::ivec3 &pointed );
    void cleanup( );
};

#define MAX_LOADED_CHUNKS ( ( 2 * CHUNK_RADIUS_X + 1 ) * ( 2 * CHUNK_RADIUS_Y + 1 ) * ( 2 * CHUNK_RADIUS_Z + 1 ) )

#endif