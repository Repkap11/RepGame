#pragma once

// A chunk's per-RenderOrder draw state: the GL objects (va/vb_coords) are
// created lazily by Chunk::ensure_gl_init on the render thread, while the
// CPU-side fields (populated_blocks, num_instances, ib slices) are written
// earlier by meshing workers — so the array is heap-allocated before either
// use (Chunk::init / calculate_populated_blocks) and freed in
// release_terrain. Kept out of chunk.hpp behind a pointer so the renderer
// headers don't fan out to every translation unit that touches a chunk.

#include "common/block.hpp"        // BlockCoords
#include "common/render_order.hpp" // LAST_RENDER_ORDER
#include "renderer/renderer.hpp"   // VertexArray, VertexBuffer (by value)

class RenderLayer {
    friend class Chunk;
    // ChunkLoader::rebuild_drawable_list reads num_instances to build the
    // blended subset list (Translucent/Water layers with geometry).
    friend class ChunkLoader;

    VertexArray va;
    int num_instances;
    VertexBuffer vb_coords;
    BlockCoords *populated_blocks;
    // Index data is a pure function of the chunk's 6-bit face-visibility mask,
    // so all variants live in one shared element buffer (chunk.cpp) and each
    // layer stores {byte offset, element count} per pass: [0]=normal,
    // [1]=reflect. count==0 draws nothing (e.g. water has no reflect variant).
    unsigned int ib_offset[ 2 ];
    unsigned int ib_count[ 2 ];
};
