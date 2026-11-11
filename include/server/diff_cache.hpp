#pragma once

// Server-side LRU cache of ENCODED CHUNK_DIFF_RESULT payload bytes for
// clean chunks (those without pending edits in ServerLogic::world_cache).
// Diff requests for unedited chunks otherwise decode the chunk_<x>_<y>_<z>
// edit file and re-serialize the identical payload on every request box.
//
// Bounded two ways: a byte budget (payload bytes + ENTRY_OVERHEAD charged
// per entry, evicted least-recently-used first) and a hard entry cap so a
// flood of tiny payloads can't grow bookkeeping unboundedly. A payload
// larger than the byte budget is never cached. Not thread-safe — the
// dedicated server drives it from its single event loop.

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

class DiffCache {
    // List of packed chunk keys, MRU at front; the map holds each entry's
    // list iterator for O(1) promote/erase.
    struct Entry {
        std::list<uint64_t>::iterator lru_it;
        std::vector<uint8_t> payload;
    };
    std::list<uint64_t> lru_;
    std::unordered_map<uint64_t, Entry> map_;
    size_t byte_budget_ = 0;
    size_t max_entries_ = 0;
    // Accounted bytes: sum(payload.size()) + entries * ENTRY_OVERHEAD.
    size_t bytes_ = 0;
    size_t hits_ = 0;
    size_t misses_ = 0;
    size_t evictions_ = 0;

    void evict_lru( );
    void enforce_limits( );

  public:
    // Fixed accounting overhead charged per entry on top of payload bytes
    // (list node + unordered_map node + vector bookkeeping).
    static constexpr size_t ENTRY_OVERHEAD = 96;

    // Sets the byte budget and entry cap; evicts LRU-first until both
    // hold. A 0 budget or 0 cap disables caching entirely.
    void set_limits( size_t byte_budget, size_t max_entries );

    // The stored payload for `pos`, or nullptr. A hit promotes the entry
    // to most-recently-used and counts on hits(); a lookup that finds
    // nothing counts on misses().
    const std::vector<uint8_t> *find( const glm::ivec3 &pos );
    // Caches `payload` under `pos` (replacing any existing entry), then
    // evicts LRU-first until within limits. Payloads exceeding the byte
    // budget (or any insert while the cap is 0) are dropped, not cached.
    void insert( const glm::ivec3 &pos, std::vector<uint8_t> payload );
    void erase( const glm::ivec3 &pos );
    void clear( );

    size_t entries( ) const {
        return this->map_.size( );
    }
    size_t bytes( ) const {
        return this->bytes_;
    }
    size_t byte_budget( ) const {
        return this->byte_budget_;
    }
    size_t max_entries( ) const {
        return this->max_entries_;
    }
    size_t hits( ) const {
        return this->hits_;
    }
    size_t misses( ) const {
        return this->misses_;
    }
    size_t evictions( ) const {
        return this->evictions_;
    }
};
