#include <string.h>
#include <stdlib.h>
#include <vector>

#include "common/RepGame.hpp"
#include "common/chunk.hpp"

// Chunk buffers are fixed-size and never resized, so freed buffers go into
// small per-thread free lists rather than back to malloc: multi-megabyte
// allocations bypass glibc's arena and hit mmap/munmap (+ page faults on
// first touch) every time, which slots recycling between terrain (allocated)
// and air (freed) chunks would otherwise pay constantly. thread_local means
// no locking; a buffer freed on a different thread just joins that thread's
// pool. The cap bounds retained memory (8 * ~3.7MB for blocks per thread).
namespace {
constexpr int CHUNK_BUF_POOL_MAX = 8;
thread_local std::vector<void *> s_pool_blocks;
thread_local std::vector<void *> s_pool_light;
thread_local std::vector<void *> s_pool_light_columns;
thread_local std::vector<void *> s_pool_air_mask;

template <typename T>
static T *chunk_pool_get( std::vector<void *> &pool, const size_t bytes, const bool zeroed ) {
    void *p;
    if ( !pool.empty( ) ) {
        p = pool.back( );
        pool.pop_back( );
        if ( zeroed ) {
            memset( p, 0, bytes );
        }
    } else {
        p = zeroed ? calloc( bytes, 1 ) : malloc( bytes );
    }
    return static_cast<T *>( p );
}

static void chunk_pool_put( std::vector<void *> &pool, void *p ) {
    if ( !p ) {
        return;
    }
    if ( pool.size( ) < CHUNK_BUF_POOL_MAX ) {
        pool.push_back( p );
    } else {
        free( p );
    }
}
} // namespace

BlockState *chunk_alloc_blocks( ) {
    return chunk_pool_get<BlockState>( s_pool_blocks, CHUNK_BLOCK_SIZE * sizeof( BlockState ), true );
}
void chunk_free_blocks( BlockState *p ) {
    chunk_pool_put( s_pool_blocks, p );
}
unsigned char *chunk_alloc_light( ) {
    // Callers always rewrite the volume (fill memset/materialize memset).
    return chunk_pool_get<unsigned char>( s_pool_light, CHUNK_BLOCK_SIZE, false );
}
void chunk_free_light( unsigned char *p ) {
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
