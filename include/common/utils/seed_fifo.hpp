#pragma once

#include <mutex>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

// FIFO of fixed-size blocks: an empty SeedFifo allocates nothing (unlike
// std::deque, which allocates a map + block even when empty). Blocks are
// 4096 bytes and recycle through one shared free list — seeds are pushed
// and popped on different threads, and blocks move at most once per N
// pushes/pops, so a plain mutex suffices. The pool retains at most 64 MB
// of idle blocks; beyond that the block is freed.
namespace seed_fifo_detail {
constexpr size_t BLOCK_BYTES = 4096;
constexpr size_t POOL_MAX_BLOCKS = 16384; // 16384 * 4096 = 64 MB retained

struct BlockPool {
    std::mutex mu;
    std::vector<void *> free_list;
};

// Leaked on purpose: SeedFifo destructors of static-lifetime owners can run
// after a function-local static pool would already be destroyed.
inline BlockPool &block_pool( ) {
    static BlockPool *pool = new BlockPool( );
    return *pool;
}

inline void *block_get( ) {
    BlockPool &pool = block_pool( );
    {
        std::lock_guard<std::mutex> lock( pool.mu );
        if ( !pool.free_list.empty( ) ) {
            void *p = pool.free_list.back( );
            pool.free_list.pop_back( );
            return p;
        }
    }
    return malloc( BLOCK_BYTES );
}

inline void block_put( void *p ) {
    BlockPool &pool = block_pool( );
    {
        std::lock_guard<std::mutex> lock( pool.mu );
        if ( pool.free_list.size( ) < POOL_MAX_BLOCKS ) {
            pool.free_list.push_back( p );
            return;
        }
    }
    free( p );
}
} // namespace seed_fifo_detail

template <typename T>
class SeedFifo {
    static constexpr size_t N = ( seed_fifo_detail::BLOCK_BYTES - sizeof( void * ) - 2 * sizeof( uint32_t ) ) / sizeof( T );
    struct Block {
        Block *next;
        uint32_t head, tail;
        T items[ N ];
    };
    static_assert( sizeof( Block ) <= seed_fifo_detail::BLOCK_BYTES, "SeedFifo block exceeds 4KB" );

    Block *head_block = nullptr;
    Block *tail_block = nullptr;
    size_t count = 0;

  public:
    SeedFifo( ) = default;
    SeedFifo( const SeedFifo & ) = delete;
    SeedFifo &operator=( const SeedFifo & ) = delete;
    ~SeedFifo( ) {
        this->clear( );
    }

    bool empty( ) const {
        return this->count == 0;
    }
    size_t size( ) const {
        return this->count;
    }
    const T &front( ) const {
        return this->head_block->items[ this->head_block->head ];
    }

    void push_back( const T &v ) {
        if ( !this->tail_block || this->tail_block->tail == N ) {
            Block *b = static_cast<Block *>( seed_fifo_detail::block_get( ) );
            b->next = nullptr;
            b->head = 0;
            b->tail = 0;
            if ( this->tail_block ) {
                this->tail_block->next = b;
            } else {
                this->head_block = b;
            }
            this->tail_block = b;
        }
        this->tail_block->items[ this->tail_block->tail++ ] = v;
        this->count++;
    }

    void pop_front( ) {
        if ( ++this->head_block->head == this->head_block->tail ) {
            Block *b = this->head_block;
            this->head_block = b->next;
            if ( !this->head_block ) {
                this->tail_block = nullptr;
            }
            seed_fifo_detail::block_put( b );
        }
        this->count--;
    }

    void clear( ) {
        while ( this->head_block ) {
            Block *b = this->head_block;
            this->head_block = b->next;
            seed_fifo_detail::block_put( b );
        }
        this->tail_block = nullptr;
        this->count = 0;
    }
};
