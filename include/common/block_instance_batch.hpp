#pragma once

// CPU->GPU batch of BlockCoords block-model instances, drawn by
// ChunkLoader::draw through the shared chunk shader alongside terrain so
// moving block bodies render with their full chunk-mesher shape (see
// block_mesh_emit.hpp / docs/entities.md). Groups instances by render order
// and by the light-chunk containing the body centre — GLES3 has no
// glDrawElementsInstancedBaseInstance, so each group owns its own VA/VB,
// refilled per frame.

#include <map>
#include <vector>

#include "common/block.hpp"
#include "common/block_mesh_emit.hpp"
#include "common/renderer/vertex_array.hpp"

class ChunkLoader;
class VertexBufferLayout;

class BlockInstanceBatch {
  public:
    struct Group {
        std::vector<BlockCoords> cpu;
        VertexArray va;
        VertexBuffer vb;
        bool gl_ready = false;
        bool touched = false;
        bool water_layout = false;
    };
    struct Key {
        int order;
        glm::ivec3 chunk_pos;
        bool operator<( const Key &other ) const;
    };
    std::map<Key, Group> groups;

    void init( const VertexBuffer &vb_solid, const VertexBuffer &vb_water,
               const VertexBufferLayout &vbl_block, const VertexBufferLayout &vbl_coords );
    void destroy( );
    // Start a fill pass: every group begins empty; GL objects are kept.
    void begin( );
    // Sink bound to (order, light_chunk) for block_emit_cell. `reserve` must
    // be at least block_emit_count for the cell: the emitter holds slot
    // pointers across allocs, so the group's vector may not reallocate.
    BlockEmitSink sink( RenderOrder order, const glm::ivec3 &light_chunk, int reserve );
    // Upload a group's CPU list to its VB, lazily building the VA/VB pair.
    // Called by ChunkLoader::draw before iterating.
    void ensure_gl( Group &g, bool water );

  private:
    const VertexBuffer *vb_solid = nullptr;
    const VertexBuffer *vb_water = nullptr;
    const VertexBufferLayout *vbl_block = nullptr;
    const VertexBufferLayout *vbl_coords = nullptr;
};
