#include "server/diff_cache.hpp"

// Chunk coords packed into one u64 key — the same layout map_storage.cpp's
// save-file index uses: x/z 22 bits (~±2M chunks), y 20 bits (~±512K).
// Two's-complement low bits keep negative coords distinct.
static inline uint64_t diff_cache_key( const glm::ivec3 &p ) {
    return ( ( uint64_t )( p.x & 0x3FFFFF ) << 42 ) |
           ( ( uint64_t )( p.y & 0xFFFFF ) << 22 ) |
           ( ( uint64_t )( p.z & 0x3FFFFF ) );
}

void DiffCache::evict_lru( ) {
    const uint64_t key = this->lru_.back( );
    const auto it = this->map_.find( key );
    this->bytes_ -= it->second.payload.size( ) + ENTRY_OVERHEAD;
    this->map_.erase( it );
    this->lru_.pop_back( );
    this->evictions_++;
}

void DiffCache::enforce_limits( ) {
    while ( ( this->bytes_ > this->byte_budget_ || this->map_.size( ) > this->max_entries_ ) &&
            !this->map_.empty( ) ) {
        this->evict_lru( );
    }
}

void DiffCache::set_limits( size_t byte_budget, size_t max_entries ) {
    this->byte_budget_ = byte_budget;
    this->max_entries_ = max_entries;
    this->enforce_limits( );
}

const std::vector<uint8_t> *DiffCache::find( const glm::ivec3 &pos ) {
    const auto it = this->map_.find( diff_cache_key( pos ) );
    if ( it == this->map_.end( ) ) {
        this->misses_++;
        return nullptr;
    }
    this->hits_++;
    this->lru_.splice( this->lru_.begin( ), this->lru_, it->second.lru_it );
    return &it->second.payload;
}

void DiffCache::insert( const glm::ivec3 &pos, std::vector<uint8_t> payload ) {
    const uint64_t key = diff_cache_key( pos );
    // A re-insert replaces the entry outright — cheaper than growing the
    // accounting sideways through two live copies.
    this->erase( pos );
    if ( payload.size( ) + ENTRY_OVERHEAD > this->byte_budget_ || this->max_entries_ == 0 ) {
        // Oversized for the budget (or caching disabled): never cached.
        return;
    }
    this->lru_.push_front( key );
    Entry e;
    e.lru_it = this->lru_.begin( );
    e.payload = std::move( payload );
    this->bytes_ += e.payload.size( ) + ENTRY_OVERHEAD;
    this->map_.emplace( key, std::move( e ) );
    this->enforce_limits( );
}

void DiffCache::erase( const glm::ivec3 &pos ) {
    const auto it = this->map_.find( diff_cache_key( pos ) );
    if ( it == this->map_.end( ) ) {
        return;
    }
    this->bytes_ -= it->second.payload.size( ) + ENTRY_OVERHEAD;
    this->lru_.erase( it->second.lru_it );
    this->map_.erase( it );
}

void DiffCache::clear( ) {
    this->lru_.clear( );
    this->map_.clear( );
    this->bytes_ = 0;
}
