#include <string.h>
#include <stdlib.h>
#include <mutex>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk.hpp"

// Chunk buffers are fixed-size and never resized, so freed buffers go into
// free lists rather than back to malloc: multi-megabyte allocations bypass
// glibc's arena and hit mmap/munmap (+ page faults on first touch) every
// time, which slots recycling between terrain (allocated) and air (freed)
// chunks would otherwise pay constantly. The lists are shared across
// threads — buffers are allocated on terrain worker threads but mostly
// freed on the main thread, so per-thread lists rarely saw hits — guarded
// by a mutex; zeroing and malloc/free run outside the lock. Each pool's
// cap bounds retained memory to CHUNK_BUF_POOL_BUDGET_BYTES per buffer
// type.
namespace {
constexpr size_t CHUNK_BUF_POOL_BUDGET_BYTES = 64u << 20;

struct ChunkBufPool {
    explicit ChunkBufPool( size_t cap ) : cap( cap ) {
    }
    std::mutex mu;
    std::vector<void *> free_list;
    size_t cap;
};

constexpr size_t chunk_buf_pool_cap( const size_t bytes ) {
    const size_t cap = CHUNK_BUF_POOL_BUDGET_BYTES / bytes;
    return cap < 8 ? 8 : ( cap > 1024 ? 1024 : cap );
}

ChunkBufPool s_pool_cells( chunk_buf_pool_cap( CHUNK_BLOCK_SIZE * sizeof( BlockCell ) ) );
ChunkBufPool s_pool_extras( chunk_buf_pool_cap( CHUNK_BLOCK_SIZE * sizeof( BlockExtra ) ) );
ChunkBufPool s_pool_light( chunk_buf_pool_cap( CHUNK_BLOCK_SIZE * sizeof( unsigned short ) ) );
ChunkBufPool s_pool_light_columns( chunk_buf_pool_cap( 3 * LIGHT_FLAGS_COUNT ) );
ChunkBufPool s_pool_air_mask( chunk_buf_pool_cap( CHUNK_BLOCK_SIZE / 8 ) );

template <typename T>
static T *chunk_pool_get( ChunkBufPool &pool, const size_t bytes, const bool zeroed ) {
    void *p = nullptr;
    {
        std::lock_guard<std::mutex> lock( pool.mu );
        if ( !pool.free_list.empty( ) ) {
            p = pool.free_list.back( );
            pool.free_list.pop_back( );
        }
    }
    if ( p ) {
        if ( zeroed ) {
            memset( p, 0, bytes );
        }
    } else {
        p = zeroed ? calloc( bytes, 1 ) : malloc( bytes );
    }
    return static_cast<T *>( p );
}

static void chunk_pool_put( ChunkBufPool &pool, void *p ) {
    if ( !p ) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock( pool.mu );
        if ( pool.free_list.size( ) < pool.cap ) {
            pool.free_list.push_back( p );
            return;
        }
    }
    free( p );
}
} // namespace

BlockCell *chunk_alloc_cells( ) {
    return chunk_pool_get<BlockCell>( s_pool_cells, CHUNK_BLOCK_SIZE * sizeof( BlockCell ), true );
}
void chunk_free_cells( BlockCell *p ) {
    chunk_pool_put( s_pool_cells, p );
}
BlockExtra *chunk_alloc_extras( ) {
    // Callers rewrite every entry (materialize_extras fills from cells[]).
    return chunk_pool_get<BlockExtra>( s_pool_extras, CHUNK_BLOCK_SIZE * sizeof( BlockExtra ), false );
}
void chunk_free_extras( BlockExtra *p ) {
    chunk_pool_put( s_pool_extras, p );
}
unsigned short *chunk_alloc_light( ) {
    // Callers always rewrite the volume (fill memset/materialize fill).
    return chunk_pool_get<unsigned short>( s_pool_light, CHUNK_BLOCK_SIZE * sizeof( unsigned short ), false );
}
void chunk_free_light( unsigned short *p ) {
    chunk_pool_put( s_pool_light, p );
}
unsigned char *chunk_alloc_light_columns( ) {
    return chunk_pool_get<unsigned char>( s_pool_light_columns, 3 * LIGHT_FLAGS_COUNT, true );
}
void chunk_free_light_columns( unsigned char *p ) {
    chunk_pool_put( s_pool_light_columns, p );
}
unsigned char *chunk_alloc_air_mask( ) {
    // Unzeroed is fine: the light_fill_chunk builder rewrites every bit and
    // the set_block materialize path memsets.
    return chunk_pool_get<unsigned char>( s_pool_air_mask, CHUNK_BLOCK_SIZE / 8, false );
}
void chunk_free_air_mask( unsigned char *p ) {
    chunk_pool_put( s_pool_air_mask, p );
}
