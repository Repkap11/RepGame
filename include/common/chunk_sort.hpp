#pragma once

// Camera-distance ordering for ChunkLoader::draw's drawable lists. Keys
// are computed once into a scratch vector (not per comparison). Callers
// keep the previous sorted order: the list is a sorted prefix plus an
// unsorted tail of newcomers, so a re-sort re-sorts the prefix, sorts the
// tail, and merges the two halves.

#include <algorithm>
#include <vector>

#include "chunk.hpp"

struct ChunkSortKey {
    // Sort scalar: squared camera distance for the opaque front-to-back
    // list, or view depth (projection on the camera forward, negative
    // behind the camera) for the blended painter-order list.
    float key;
    glm::ivec3 pos;
    Chunk *chunk;
};

// The strict total order both sort paths share: ascending sort key, ties
// broken lexicographically on chunk_pos so the result never depends on
// input order (the blended translucent pass has no depth writes — order is
// the composited colour).
inline bool chunk_sort_key_less( const ChunkSortKey &a, const ChunkSortKey &b ) {
    if ( a.key != b.key ) {
        return a.key < b.key;
    }
    if ( a.pos.x != b.pos.x ) {
        return a.pos.x < b.pos.x;
    }
    if ( a.pos.y != b.pos.y ) {
        return a.pos.y < b.pos.y;
    }
    return a.pos.z < b.pos.z;
}

// Sorts list[0..n) ascending by chunk_sort_key_less for cam. With
// view_dir=nullptr the key is squared distance from each chunk centre
// (chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 - render_origin) to cam;
// otherwise it is view depth (dot of that offset with the normalized
// camera forward) — painter order needs depth along the view, since
// radial distance misorders an off-axis far surface as nearer than an
// on-axis near one. list[0..sorted_n) is in a sorted order for some
// earlier camera; list[sorted_n..n) is unsorted. Keys are computed once
// into scratch. The prefix is re-sorted for cam by insertion sort
// (full_prefix=false, cheap when the camera barely moved) or std::sort
// (full_prefix=true, for big camera jumps), the tail by std::sort, then
// the two sorted halves are merged (in place — both inputs are already
// sorted, which is all the merge requires). Result: list[0..n) exactly
// sorted by chunk_sort_key_less for cam.
inline void sort_chunks_by_distance( Chunk **list, int n, int sorted_n, const glm::vec3 &render_origin, const glm::vec3 &cam, const glm::vec3 *view_dir, bool full_prefix, std::vector<ChunkSortKey> &scratch ) {
    scratch.resize( n );
    for ( int i = 0; i < n; i++ ) {
        Chunk *chunk = list[ i ];
        const glm::vec3 d = glm::vec3( chunk->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 ) - render_origin - cam;
        scratch[ i ] = { view_dir ? glm::dot( d, *view_dir ) : glm::dot( d, d ), chunk->chunk_pos, chunk };
    }
    if ( full_prefix ) {
        std::sort( scratch.begin( ), scratch.begin( ) + sorted_n, chunk_sort_key_less );
    } else {
        for ( int i = 1; i < sorted_n; i++ ) {
            const ChunkSortKey key = scratch[ i ];
            int j = i - 1;
            while ( j >= 0 && chunk_sort_key_less( key, scratch[ j ] ) ) {
                scratch[ j + 1 ] = scratch[ j ];
                j--;
            }
            scratch[ j + 1 ] = key;
        }
    }
    std::sort( scratch.begin( ) + sorted_n, scratch.end( ), chunk_sort_key_less );
    std::inplace_merge( scratch.begin( ), scratch.begin( ) + sorted_n, scratch.end( ), chunk_sort_key_less );
    for ( int i = 0; i < n; i++ ) {
        list[ i ] = scratch[ i ].chunk;
    }
}
