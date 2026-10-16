#include <hip/hip_runtime.h>
#include <mutex>

#include "common/Logging.hpp"
#include "linux/hip/mesh_gen.hpp"
#include "common/block.hpp"
#include "common/render_order.hpp"

// Device-side constants (must match CPU definitions exactly)
#define FACE_TOP_D 0
#define FACE_BOTTOM_D 1
#define FACE_RIGHT_D 2
#define FACE_FRONT_D 3
#define FACE_LEFT_D 4
#define FACE_BACK_D 5

__device__ static constexpr int FACE_DIR_X[ 6 ] = { 0, 0, 1, 0, -1, 0 };
__device__ static constexpr int FACE_DIR_Y[ 6 ] = { 1, -1, 0, 0, 0, 0 };
__device__ static constexpr int FACE_DIR_Z[ 6 ] = { 0, 0, 0, 1, 0, -1 };

__device__ static constexpr int OPPOSITE_FACE_D[ 6 ] = { 1, 0, 4, 5, 2, 3 };

// ROTATED_FACE_TABLE[rotation][face] - must match chunk.cpp exactly
__device__ static constexpr int ROTATED_FACE_TABLE_D[ 4 ][ 6 ] = {
    { 0, 1, 2, 3, 4, 5 }, // rotation 0
    { 0, 1, 5, 2, 3, 4 }, // rotation 1
    { 0, 1, 4, 5, 2, 3 }, // rotation 2
    { 0, 1, 3, 4, 5, 2 }, // rotation 3
};

// RenderOrder enum values (from render_order.hpp)
#define RENDER_ORDER_TRANSPARENT 0
#define RENDER_ORDER_TRANSLUCENT 1
#define RENDER_ORDER_WATER 2
#define RENDER_ORDER_FLOWERS 3
#define RENDER_ORDER_OPAQUE 4

__device__ static inline int get_index_from_coords_d( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( x + 1 ) * CHUNK_SIZE_INTERNAL_Z + ( z + 1 );
}

// Mirror of face_is_seethrough in chunk.cpp: a powered piston base recesses
// 4px at its head end, so every def-space face except the covered back face
// lets neighbor faces show through. Only used for covering-block lookups.
__device__ static inline int face_is_seethrough_d( const GPUBlockDef &block, const BlockState &state, int face ) {
    if ( block.is_piston && state.current_redstone_power > 0 ) {
        // piston_base_covered_face: 0-3 -> FACE_BACK, 4 -> FACE_BOTTOM, 5 -> FACE_TOP
        int covered = state.rotation >= 4 ? ( state.rotation == 4 ? FACE_BOTTOM_D : FACE_TOP_D ) : FACE_BACK_D;
        return face != covered;
    }
    return block.is_seethrough_face[ face ];
}

__global__ void hip_calculate_mesh_kernel( const BlockState *blocks,
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

    BlockState blockState = blocks[ index ];
    GPUBlockDef block = block_defs[ blockState.id ];

    // render_order_is_visible: renderOrder != RenderOrder_Transparent
    results[ index ].visible = ( block.renderOrder != RENDER_ORDER_TRANSPARENT );

    if ( !results[ index ].visible ) {
        results[ index ].can_be_seen = false;
        return;
    }

    int block_is_visiable = 0;
    int visible_from[ 6 ] = { 0, 0, 0, 0, 0, 0 };

    for ( int face = 0; face < 6; face++ ) {
        int rotated_face = ROTATED_FACE_TABLE_D[ blockState.rotation & 3 ][ face ];
        int neighbor_idx = get_index_from_coords_d( x + FACE_DIR_X[ face ], y + FACE_DIR_Y[ face ], z + FACE_DIR_Z[ face ] );
        BlockState neighbor_state = blocks[ neighbor_idx ];
        GPUBlockDef neighbor_block = block_defs[ neighbor_state.id ];

        int opposing_face = OPPOSITE_FACE_D[ ROTATED_FACE_TABLE_D[ neighbor_state.rotation & 3 ][ face ] ];

        int is_seethrough = block.is_seethrough_face[ rotated_face ];
        if ( is_seethrough && block.hides_self[ rotated_face ] ) {
            // Waterlogged cells count as water: no surface faces against them.
            int liquid_boundary = ( block.renderOrder == RENDER_ORDER_WATER && neighbor_state.waterlogged );
            if ( face == FACE_TOP_D && block.renderOrder == RENDER_ORDER_WATER ) {
                visible_from[ face ] = ( neighbor_block.id != block.id && !liquid_boundary );
            } else {
                visible_from[ face ] = ( face_is_seethrough_d( neighbor_block, neighbor_state, opposing_face ) && neighbor_state.id != blockState.id && !liquid_boundary );
            }
        } else if ( is_seethrough ) {
            int visable_through_opposing = 0;
            for ( int face_around = 0; face_around < 6; face_around++ ) {
                if ( face_around == opposing_face )
                    continue;
                int around_idx = get_index_from_coords_d( x + FACE_DIR_X[ face_around ], y + FACE_DIR_Y[ face_around ], z + FACE_DIR_Z[ face_around ] );
                BlockState block_around_state = blocks[ around_idx ];
                GPUBlockDef block_around = block_defs[ block_around_state.id ];
                int around_face = OPPOSITE_FACE_D[ ROTATED_FACE_TABLE_D[ block_around_state.rotation & 3 ][ face_around ] ];
                if ( face_is_seethrough_d( block_around, block_around_state, around_face ) ) {
                    visable_through_opposing = 1;
                    break;
                }
            }
            visible_from[ face ] = visable_through_opposing;
        } else {
            visible_from[ face ] = face_is_seethrough_d( neighbor_block, neighbor_state, opposing_face );
        }
        block_is_visiable |= visible_from[ face ];
    }

    // render_order_can_be_shaded: Opaque or Translucent
    int can_be_shaded = ( block.renderOrder == RENDER_ORDER_OPAQUE || block.renderOrder == RENDER_ORDER_TRANSLUCENT );

    // Per-vertex corner AO removed: the flood-fill light volume already
    // darkens crevices, so visible faces are packed as fully lit
    // (NO_LIGHT_BRIGHT = 0). Hidden faces keep the block's no_light sentinel
    // (NO_LIGHT_NO_DRAW clips the quad in the shader).
    unsigned int lit = can_be_shaded ? 0u : ( unsigned int )block.no_light;
    for ( int i = 0; i < 6; i++ ) {
        results[ index ].packed_lighting[ i ] = visible_from[ i ] ? lit : ( unsigned int )block.no_light;
    }

    results[ index ].can_be_seen = block_is_visiable;
}

// Static device buffers (initialized once, shared across all threads via single GPU device)
static GPUBlockDef *d_block_defs = nullptr;
static bool mesh_gen_initialized = false;
static std::mutex mesh_gen_init_mutex;

// Thread-local device buffers for per-chunk operations
static thread_local BlockState *d_blocks = nullptr;
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

        // Upload compact block definitions
        GPUBlockDef *host_defs = new GPUBlockDef[ LAST_BLOCK_ID ];
        for ( int i = 0; i < LAST_BLOCK_ID; i++ ) {
            host_defs[ i ].renderOrder = block_definitions[ i ].renderOrder;
            host_defs[ i ].no_light = block_definitions[ i ].no_light;
            host_defs[ i ].id = block_definitions[ i ].id;
            host_defs[ i ].is_piston = block_definitions[ i ].is_piston ? 1 : 0;
            for ( int j = 0; j < NUM_FACES_IN_CUBE; j++ ) {
                host_defs[ i ].is_seethrough_face[ j ] = block_definitions[ i ].calculated.is_seethrough_face[ j ];
                host_defs[ i ].hides_self[ j ] = block_definitions[ i ].calculated.hides_self[ j ];
            }
        }

        HIP_CHECK( hipMalloc( &d_block_defs, LAST_BLOCK_ID * sizeof( GPUBlockDef ) ) );
        HIP_CHECK( hipMemcpy( d_block_defs, host_defs, LAST_BLOCK_ID * sizeof( GPUBlockDef ), hipMemcpyHostToDevice ) );

        delete[] host_defs;

        mesh_gen_initialized = true;
    }
    return true;
}

bool mesh_gen_calculate_hip( const BlockState *blocks, WorkingSpace *workingSpace ) {
    if ( !mesh_gen_init_hip( ) )
        return false;

    // Allocate thread-local device buffers on first use
    if ( !d_blocks ) {
        HIP_CHECK( hipMalloc( &d_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ) ) );
        HIP_CHECK( hipMalloc( &d_results, CHUNK_BLOCK_SIZE * sizeof( MeshResult ) ) );
    }

    // Upload block data
    HIP_CHECK( hipMemcpy( d_blocks, blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ), hipMemcpyHostToDevice ) );

    // Zero results (border blocks must be zeroed)
    HIP_CHECK( hipMemset( d_results, 0, CHUNK_BLOCK_SIZE * sizeof( MeshResult ) ) );

    // Launch kernel
    int num_blocks = ( CHUNK_BLOCK_SIZE + MESH_THREADS_PER_BLOCK - 1 ) / MESH_THREADS_PER_BLOCK;
    hip_calculate_mesh_kernel<<<num_blocks, MESH_THREADS_PER_BLOCK>>>( d_blocks, d_block_defs, d_results );

    // Wait for kernel to complete before reading back
    HIP_CHECK( hipDeviceSynchronize( ) );

    // Read back results directly into workingSpace (MeshResult matches WorkingSpace layout)
    HIP_CHECK( hipMemcpy( workingSpace, d_results, CHUNK_BLOCK_SIZE * sizeof( MeshResult ), hipMemcpyDeviceToHost ) );

    return true;
}
