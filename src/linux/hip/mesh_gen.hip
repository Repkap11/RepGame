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

// CORNER_OFFSET constants (from constants.hpp)
#define CORNER_OFFSET_tfl_D 0
#define CORNER_OFFSET_tfr_D 2
#define CORNER_OFFSET_tbl_D 4
#define CORNER_OFFSET_tbr_D 6
#define CORNER_OFFSET_bfl_D 8
#define CORNER_OFFSET_bfr_D 10
#define CORNER_OFFSET_bbl_D 12
#define CORNER_OFFSET_bbr_D 14
#define CORNER_OFFSET_c_D 16

// RenderOrder enum values (from render_order.hpp)
#define RENDER_ORDER_TRANSPARENT 0
#define RENDER_ORDER_TRANSLUCENT 1
#define RENDER_ORDER_WATER 2
#define RENDER_ORDER_FLOWERS 3
#define RENDER_ORDER_OPAQUE 4

__device__ static inline int get_index_from_coords_d( int x, int y, int z ) {
    return ( y + 1 ) * CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z + ( x + 1 ) * CHUNK_SIZE_INTERNAL_Z + ( z + 1 );
}

__device__ static inline int min4_d( const int a, const int b, const int c, const int d ) {
    const int diag1 = ( a ) + ( d );
    const int diag2 = ( b ) + ( c );
    return ( diag1 < diag2 ? diag1 : diag2 );
}

__global__ void hip_calculate_mesh_kernel( const BlockState *blocks,
                                            const GPUBlockDef *block_defs,
                                            const int *casts_shadow,
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
            if ( face == FACE_TOP_D && block.renderOrder == RENDER_ORDER_WATER ) {
                visible_from[ face ] = ( neighbor_block.id != block.id );
            } else {
                visible_from[ face ] = ( neighbor_block.is_seethrough_face[ opposing_face ] && neighbor_state.id != blockState.id );
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
                if ( block_around.is_seethrough_face[ around_face ] ) {
                    visable_through_opposing = 1;
                    break;
                }
            }
            visible_from[ face ] = visable_through_opposing;
        } else {
            visible_from[ face ] = neighbor_block.is_seethrough_face[ opposing_face ];
        }
        block_is_visiable |= visible_from[ face ];
    }

    // render_order_can_be_shaded: Opaque or Translucent
    int can_be_shaded = ( block.renderOrder == RENDER_ORDER_OPAQUE || block.renderOrder == RENDER_ORDER_TRANSLUCENT );

    int xplus = x + 1, xminus = x - 1;
    int yplus = y + 1, yminus = y - 1;
    int zplus = z + 1, zminus = z - 1;

    int t = casts_shadow[ blocks[ get_index_from_coords_d( x, yplus, z ) ].id ];
    int bo = casts_shadow[ blocks[ get_index_from_coords_d( x, yminus, z ) ].id ];
    int f = casts_shadow[ blocks[ get_index_from_coords_d( x, y, zplus ) ].id ];
    int ba = casts_shadow[ blocks[ get_index_from_coords_d( x, y, zminus ) ].id ];
    int r = casts_shadow[ blocks[ get_index_from_coords_d( xplus, y, z ) ].id ];
    int l = casts_shadow[ blocks[ get_index_from_coords_d( xminus, y, z ) ].id ];

    int tl = casts_shadow[ blocks[ get_index_from_coords_d( xminus, yplus, z ) ].id ];
    int tr = casts_shadow[ blocks[ get_index_from_coords_d( xplus, yplus, z ) ].id ];
    int tf = casts_shadow[ blocks[ get_index_from_coords_d( x, yplus, zplus ) ].id ];
    int tba = casts_shadow[ blocks[ get_index_from_coords_d( x, yplus, zminus ) ].id ];

    int bol = casts_shadow[ blocks[ get_index_from_coords_d( xminus, yminus, z ) ].id ];
    int bor = casts_shadow[ blocks[ get_index_from_coords_d( xplus, yminus, z ) ].id ];
    int bof = casts_shadow[ blocks[ get_index_from_coords_d( x, yminus, zplus ) ].id ];
    int boba = casts_shadow[ blocks[ get_index_from_coords_d( x, yminus, zminus ) ].id ];

    int fl = casts_shadow[ blocks[ get_index_from_coords_d( xminus, y, zplus ) ].id ];
    int bal = casts_shadow[ blocks[ get_index_from_coords_d( xminus, y, zminus ) ].id ];
    int fr = casts_shadow[ blocks[ get_index_from_coords_d( xplus, y, zplus ) ].id ];
    int bar = casts_shadow[ blocks[ get_index_from_coords_d( xplus, y, zminus ) ].id ];

    int tfl = casts_shadow[ blocks[ get_index_from_coords_d( xminus, yplus, zplus ) ].id ];
    int tbl = casts_shadow[ blocks[ get_index_from_coords_d( xminus, yplus, zminus ) ].id ];
    int tfr = casts_shadow[ blocks[ get_index_from_coords_d( xplus, yplus, zplus ) ].id ];
    int tbr = casts_shadow[ blocks[ get_index_from_coords_d( xplus, yplus, zminus ) ].id ];

    int bfl = casts_shadow[ blocks[ get_index_from_coords_d( xminus, yminus, zplus ) ].id ];
    int bbl = casts_shadow[ blocks[ get_index_from_coords_d( xminus, yminus, zminus ) ].id ];
    int bfr = casts_shadow[ blocks[ get_index_from_coords_d( xplus, yminus, zplus ) ].id ];
    int bbr = casts_shadow[ blocks[ get_index_from_coords_d( xplus, yminus, zminus ) ].id ];

    if ( visible_from[ FACE_TOP_D ] && can_be_shaded ) {
        int top_tfr = ( tf && tr ? 3 : ( tf + tr + tfr ) ) + t;
        int top_tfl = ( tf && tl ? 3 : ( tf + tl + tfl ) ) + t;
        int top_tbr = ( tba && tr ? 3 : ( tba + tr + tbr ) ) + t;
        int top_tbl = ( tba && tl ? 3 : ( tba + tl + tbl ) ) + t;
        top_tfr = top_tfr > 3 ? 3 : top_tfr;
        top_tfl = top_tfl > 3 ? 3 : top_tfl;
        top_tbr = top_tbr > 3 ? 3 : top_tbr;
        top_tbl = top_tbl > 3 ? 3 : top_tbl;
        int tcc = min4_d( top_tfr, top_tfl, top_tbr, top_tbl );
        results[ index ].packed_lighting[ FACE_TOP_D ] =
            ( ( top_tfr << CORNER_OFFSET_tfr_D ) | ( top_tfl << CORNER_OFFSET_tfl_D ) |
              ( top_tbr << CORNER_OFFSET_tbr_D ) | ( top_tbl << CORNER_OFFSET_tbl_D ) |
              tcc << CORNER_OFFSET_c_D );
    } else {
        results[ index ].packed_lighting[ FACE_TOP_D ] = block.no_light;
    }
    if ( visible_from[ FACE_BOTTOM_D ] && can_be_shaded ) {
        int bottom_bfr = ( bof && bor ? 3 : ( bof + bor + bfr ) ) + bo;
        int bottom_bfl = ( bof && bol ? 3 : ( bof + bol + bfl ) ) + bo;
        int bottom_bbr = ( boba && bor ? 3 : ( boba + bor + bbr ) ) + bo;
        int bottom_bbl = ( boba && bol ? 3 : ( boba + bol + bbl ) ) + bo;
        bottom_bfr = bottom_bfr > 3 ? 3 : bottom_bfr;
        bottom_bfl = bottom_bfl > 3 ? 3 : bottom_bfl;
        bottom_bbr = bottom_bbr > 3 ? 3 : bottom_bbr;
        bottom_bbl = bottom_bbl > 3 ? 3 : bottom_bbl;
        int bcc = min4_d( bottom_bfr, bottom_bfl, bottom_bbr, bottom_bbl );
        results[ index ].packed_lighting[ FACE_BOTTOM_D ] =
            ( ( bottom_bfr << CORNER_OFFSET_bfr_D ) | ( bottom_bfl << CORNER_OFFSET_bfl_D ) |
              ( bottom_bbr << CORNER_OFFSET_bbr_D ) | ( bottom_bbl << CORNER_OFFSET_bbl_D ) |
              bcc << CORNER_OFFSET_c_D );
    } else {
        results[ index ].packed_lighting[ FACE_BOTTOM_D ] = block.no_light;
    }
    if ( visible_from[ FACE_FRONT_D ] && can_be_shaded ) {
        int front_tfr = ( tf && fr ? 3 : ( tf + fr + tfr ) ) + f;
        int front_tfl = ( tf && fl ? 3 : ( tf + fl + tfl ) ) + f;
        int front_bfr = ( bof && fr ? 3 : ( bof + fr + bfr ) ) + f;
        int front_bfl = ( bof && fl ? 3 : ( bof + fl + bfl ) ) + f;
        front_tfr = front_tfr > 3 ? 3 : front_tfr;
        front_tfl = front_tfl > 3 ? 3 : front_tfl;
        front_bfr = front_bfr > 3 ? 3 : front_bfr;
        front_bfl = front_bfl > 3 ? 3 : front_bfl;
        int cfc = min4_d( front_tfr, front_tfl, front_bfr, front_bfl );
        results[ index ].packed_lighting[ FACE_FRONT_D ] =
            ( ( front_tfr << CORNER_OFFSET_tfr_D ) | ( front_tfl << CORNER_OFFSET_tfl_D ) |
              ( front_bfr << CORNER_OFFSET_bfr_D ) | ( front_bfl << CORNER_OFFSET_bfl_D ) |
              cfc << CORNER_OFFSET_c_D );
    } else {
        results[ index ].packed_lighting[ FACE_FRONT_D ] = block.no_light;
    }
    if ( visible_from[ FACE_BACK_D ] && can_be_shaded ) {
        int back_tbr = ( tba && bar ? 3 : ( tba + bar + tbr ) ) + ba;
        int back_tbl = ( tba && bal ? 3 : ( tba + bal + tbl ) ) + ba;
        int back_bbr = ( boba && bar ? 3 : ( boba + bar + bbr ) ) + ba;
        int back_bbl = ( boba && bal ? 3 : ( boba + bal + bbl ) ) + ba;
        back_tbr = back_tbr > 3 ? 3 : back_tbr;
        back_tbl = back_tbl > 3 ? 3 : back_tbl;
        back_bbr = back_bbr > 3 ? 3 : back_bbr;
        back_bbl = back_bbl > 3 ? 3 : back_bbl;
        int cbc = min4_d( back_tbr, back_tbl, back_bbr, back_bbl );
        results[ index ].packed_lighting[ FACE_BACK_D ] =
            ( ( back_tbr << CORNER_OFFSET_tbr_D ) | ( back_tbl << CORNER_OFFSET_tbl_D ) |
              ( back_bbr << CORNER_OFFSET_bbr_D ) | ( back_bbl << CORNER_OFFSET_bbl_D ) |
              cbc << CORNER_OFFSET_c_D );
    } else {
        results[ index ].packed_lighting[ FACE_BACK_D ] = block.no_light;
    }
    if ( visible_from[ FACE_RIGHT_D ] && can_be_shaded ) {
        int right_tfr = ( tr && fr ? 3 : ( tr + fr + tfr ) ) + r;
        int right_tbr = ( tr && bar ? 3 : ( tr + bar + tbr ) ) + r;
        int right_bfr = ( bor && fr ? 3 : ( bor + fr + bfr ) ) + r;
        int right_bbr = ( bor && bar ? 3 : ( bor + bar + bbr ) ) + r;
        right_tfr = right_tfr > 3 ? 3 : right_tfr;
        right_tbr = right_tbr > 3 ? 3 : right_tbr;
        right_bfr = right_bfr > 3 ? 3 : right_bfr;
        right_bbr = right_bbr > 3 ? 3 : right_bbr;
        int ccr = min4_d( right_tfr, right_tbr, right_bfr, right_bbr );
        results[ index ].packed_lighting[ FACE_RIGHT_D ] =
            ( ( right_tfr << CORNER_OFFSET_tfr_D ) | ( right_tbr << CORNER_OFFSET_tbr_D ) |
              ( right_bfr << CORNER_OFFSET_bfr_D ) | ( right_bbr << CORNER_OFFSET_bbr_D ) |
              ccr << CORNER_OFFSET_c_D );
    } else {
        results[ index ].packed_lighting[ FACE_RIGHT_D ] = block.no_light;
    }
    if ( visible_from[ FACE_LEFT_D ] && can_be_shaded ) {
        int left_tfl = ( tl && fl ? 3 : ( tl + fl + tfl ) ) + l;
        int left_tbl = ( tl && bal ? 3 : ( tl + bal + tbl ) ) + l;
        int left_bfl = ( bol && fl ? 3 : ( bol + fl + bfl ) ) + l;
        int left_bbl = ( bol && bal ? 3 : ( bol + bal + bbl ) ) + l;
        left_tfl = left_tfl > 3 ? 3 : left_tfl;
        left_tbl = left_tbl > 3 ? 3 : left_tbl;
        left_bfl = left_bfl > 3 ? 3 : left_bfl;
        left_bbl = left_bbl > 3 ? 3 : left_bbl;
        int ccl = min4_d( left_tfl, left_tbl, left_bfl, left_bbl );
        results[ index ].packed_lighting[ FACE_LEFT_D ] =
            ( ( left_tfl << CORNER_OFFSET_tfl_D ) | ( left_tbl << CORNER_OFFSET_tbl_D ) |
              ( left_bfl << CORNER_OFFSET_bfl_D ) | ( left_bbl << CORNER_OFFSET_bbl_D ) |
              ccl << CORNER_OFFSET_c_D );
    } else {
        results[ index ].packed_lighting[ FACE_LEFT_D ] = block.no_light;
    }

    results[ index ].can_be_seen = block_is_visiable;
}

// Static device buffers (initialized once, shared across all threads via single GPU device)
static GPUBlockDef *d_block_defs = nullptr;
static int *d_casts_shadow = nullptr;
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
        int *host_shadow = new int[ LAST_BLOCK_ID ];
        for ( int i = 0; i < LAST_BLOCK_ID; i++ ) {
            host_defs[ i ].renderOrder = block_definitions[ i ].renderOrder;
            host_defs[ i ].no_light = block_definitions[ i ].no_light;
            host_defs[ i ].id = block_definitions[ i ].id;
            for ( int j = 0; j < NUM_FACES_IN_CUBE; j++ ) {
                host_defs[ i ].is_seethrough_face[ j ] = block_definitions[ i ].calculated.is_seethrough_face[ j ];
                host_defs[ i ].hides_self[ j ] = block_definitions[ i ].calculated.hides_self[ j ];
            }
            host_shadow[ i ] = block_definitions[ i ].casts_shadow ? 1 : 0;
        }

        HIP_CHECK( hipMalloc( &d_block_defs, LAST_BLOCK_ID * sizeof( GPUBlockDef ) ) );
        HIP_CHECK( hipMemcpy( d_block_defs, host_defs, LAST_BLOCK_ID * sizeof( GPUBlockDef ), hipMemcpyHostToDevice ) );
        HIP_CHECK( hipMalloc( &d_casts_shadow, LAST_BLOCK_ID * sizeof( int ) ) );
        HIP_CHECK( hipMemcpy( d_casts_shadow, host_shadow, LAST_BLOCK_ID * sizeof( int ), hipMemcpyHostToDevice ) );

        delete[] host_defs;
        delete[] host_shadow;

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
    hip_calculate_mesh_kernel<<<num_blocks, MESH_THREADS_PER_BLOCK>>>( d_blocks, d_block_defs, d_casts_shadow, d_results );

    // Wait for kernel to complete before reading back
    HIP_CHECK( hipDeviceSynchronize( ) );

    // Read back results directly into workingSpace (MeshResult matches WorkingSpace layout)
    HIP_CHECK( hipMemcpy( workingSpace, d_results, CHUNK_BLOCK_SIZE * sizeof( MeshResult ), hipMemcpyDeviceToHost ) );

    return true;
}
