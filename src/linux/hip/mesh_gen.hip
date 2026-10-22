// HIP phase-1 mesh kernel — the per-cell visibility + packed-lighting logic
// is NOT written here: it comes from mesh_vis_logic.hpp, the same dialect
// source the CPU loop (chunk.cpp) and the WGSL WebGPU kernel consume.
// This file provides only the kernel scaffolding (index decode, buffer
// management, launch + readback).

#include <hip/hip_runtime.h>
#include <mutex>

#include "common/Logging.hpp"
#include "linux/hip/mesh_gen.hpp"
#include "common/mesh_gen.hpp"
#include "common/block.hpp"
#include "common/render_order.hpp"

#define MG_QUAL __device__ static
#include "common/mesh_gen_fns.hpp"

__global__ void hip_calculate_mesh_kernel( const BlockCell *cells,
                                            const GPUBlockDef *block_defs,
                                            MeshResult *results ) {
    int index = blockIdx.x * blockDim.x + threadIdx.x;
    if ( index < CHUNK_BLOCK_DRAW_START || index >= CHUNK_BLOCK_DRAW_STOP )
        return;

    int y = ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1;
    int x = ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1;
    int z = ( index % CHUNK_SIZE_INTERNAL_Z ) - 1;

    // Skip border blocks (matches CPU get_coords_from_index return check).
    // Border blocks can have neighbors outside the internal volume, causing OOB access.
    if ( x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE_X || y >= CHUNK_SIZE_Y || z >= CHUNK_SIZE_Z )
        return;

#define MESH_CELL_AT( i ) ( (unsigned)( cells[ i ] ) )
#define MESH_OUT_FLAGS( idx, seen, vis )            \
    results[ idx ].can_be_seen = ( seen );          \
    results[ idx ].visible = ( vis );
#define MESH_OUT_LIGHT( idx, f, v ) results[ idx ].packed_lighting[ f ] = ( v );

#include "common/mesh_vis_logic.hpp"

#undef MESH_CELL_AT
#undef MESH_OUT_FLAGS
#undef MESH_OUT_LIGHT
}

// Static device buffers (initialized once, shared across all threads via single GPU device)
static GPUBlockDef *d_block_defs = nullptr;
static bool mesh_gen_initialized = false;
static std::mutex mesh_gen_init_mutex;

// Thread-local device buffers for per-chunk operations
static thread_local BlockCell *d_cells = nullptr;
static thread_local MeshResult *d_results = nullptr;

#define MESH_THREADS_PER_BLOCK 256

#define HIP_CHECK( expr )                                                                                              \
    do {                                                                                                                \
        hipError_t err = ( expr );                                                                                      \
        if ( err != hipSuccess ) {                                                                                       \
            pr_debug( "HIP error at %s:%d: %s", __FILE__, __LINE__, hipGetErrorString( err ) );                          \
            return false;                                                                                               \
        }                                                                                                               \
    } while ( 0 )

bool mesh_gen_init_hip( ) {
    {
        std::lock_guard<std::mutex> lock( mesh_gen_init_mutex );
        if ( mesh_gen_initialized )
            return true;

        // Upload the shared compact block-definition table — the same host
        // data the CPU mesh loop reads, so all backends see one source.
        const GPUBlockDef *host_defs = mesh_def_table( );

        HIP_CHECK( hipMalloc( &d_block_defs, LAST_BLOCK_ID * sizeof( GPUBlockDef ) ) );
        HIP_CHECK( hipMemcpy( d_block_defs, host_defs, LAST_BLOCK_ID * sizeof( GPUBlockDef ), hipMemcpyHostToDevice ) );

        mesh_gen_initialized = true;
    }
    return true;
}

bool mesh_gen_calculate_hip( const BlockCell *cells, WorkingSpace *workingSpace ) {
    if ( cells == nullptr )
        return false;
    if ( !mesh_gen_init_hip( ) )
        return false;

    // Allocate thread-local device buffers on first use
    if ( !d_cells ) {
        HIP_CHECK( hipMalloc( &d_cells, CHUNK_BLOCK_SIZE * sizeof( BlockCell ) ) );
        HIP_CHECK( hipMalloc( &d_results, CHUNK_BLOCK_SIZE * sizeof( MeshResult ) ) );
    }

    // Upload cell data — the powered flag (bit 14) and rotation (bits
    // [11:13]) ride in the cell word, so extras are never needed on-GPU.
    HIP_CHECK( hipMemcpy( d_cells, cells, CHUNK_BLOCK_SIZE * sizeof( BlockCell ), hipMemcpyHostToDevice ) );

    // Zero results (border blocks must be zeroed)
    HIP_CHECK( hipMemset( d_results, 0, CHUNK_BLOCK_SIZE * sizeof( MeshResult ) ) );

    // Launch kernel
    int num_blocks = ( CHUNK_BLOCK_SIZE + MESH_THREADS_PER_BLOCK - 1 ) / MESH_THREADS_PER_BLOCK;
    hip_calculate_mesh_kernel<<<num_blocks, MESH_THREADS_PER_BLOCK>>>( d_cells, d_block_defs, d_results );

    // Wait for kernel to complete before reading back
    HIP_CHECK( hipDeviceSynchronize( ) );

    // Read back results directly into workingSpace (MeshResult matches WorkingSpace layout)
    HIP_CHECK( hipMemcpy( workingSpace, d_results, CHUNK_BLOCK_SIZE * sizeof( MeshResult ), hipMemcpyDeviceToHost ) );

    return true;
}
