// rep_tests coverage for sort_chunks_by_distance (chunk_sort.hpp): for
// every split of the list into a sorted prefix and an unsorted newcomer
// tail, with the prefix pre-sorted for a nearby camera (and for a far
// camera under full_prefix), the merged result must equal a reference
// sort of the same strict total order (dist2 to camera, then chunk_pos
// x/y/z). n=0/n=1 must work.

#include "test_util.hpp"

#include <algorithm>
#include <vector>

#include "common/chunk_sort.hpp"

int test_chunk_sort( ) {
    int fails = 0;
    srand( 0xC0FFEE );
    std::vector<ChunkSortKey> scratch;
    const glm::vec3 ro( 0.0f );

    // Unique chunk positions on a grid around the origin.
    std::vector<glm::ivec3> positions;
    for ( int x = -8; x <= 8; x++ ) {
        for ( int y = -4; y <= 4; y++ ) {
            for ( int z = -8; z <= 8; z++ ) {
                positions.push_back( glm::ivec3( x, y, z ) );
            }
        }
    }
    std::vector<Chunk> chunks( positions.size( ) );
    std::vector<Chunk *> all;
    for ( size_t i = 0; i < positions.size( ); i++ ) {
        chunks[ i ].chunk_pos = positions[ i ];
        all.push_back( &chunks[ i ] );
    }

    // Reference implementation of the same strict total order.
    const auto dist2 = [ & ]( const Chunk *c, const glm::vec3 &cam ) {
        const glm::vec3 d = glm::vec3( c->chunk_pos * CHUNK_SIZE_I + CHUNK_SIZE_I / 2 ) - ro - cam;
        return glm::dot( d, d );
    };
    const auto reference = [ & ]( std::vector<Chunk *> list, const glm::vec3 &cam ) {
        std::sort( list.begin( ), list.end( ), [ & ]( const Chunk *a, const Chunk *b ) {
            const float da = dist2( a, cam );
            const float db = dist2( b, cam );
            if ( da != db ) {
                return da < db;
            }
            if ( a->chunk_pos.x != b->chunk_pos.x ) {
                return a->chunk_pos.x < b->chunk_pos.x;
            }
            if ( a->chunk_pos.y != b->chunk_pos.y ) {
                return a->chunk_pos.y < b->chunk_pos.y;
            }
            return a->chunk_pos.z < b->chunk_pos.z;
        } );
        return list;
    };
    const auto shuffled = [ & ]( const std::vector<Chunk *> &list ) {
        std::vector<Chunk *> copy = list;
        for ( int i = (int)copy.size( ) - 1; i > 0; i-- ) {
            std::swap( copy[ i ], copy[ rand( ) % ( i + 1 ) ] );
        }
        return copy;
    };

    // n=0 / n=1, both prefix modes, with and without a tail.
    {
        Chunk *none[ 1 ] = { nullptr };
        sort_chunks_by_distance( none, 0, 0, ro, glm::vec3( 0.0f ), false, scratch );
        sort_chunks_by_distance( none, 0, 0, ro, glm::vec3( 0.0f ), true, scratch );
        Chunk *one[ 1 ] = { all[ 0 ] };
        sort_chunks_by_distance( one, 1, 1, ro, glm::vec3( 3.0f, 4.0f, 5.0f ), false, scratch );
        fails += one[ 0 ] != all[ 0 ];
        sort_chunks_by_distance( one, 1, 0, ro, glm::vec3( 0.0f ), true, scratch );
        fails += one[ 0 ] != all[ 0 ];
        pr_test( "  sort: n=0 and n=1 survive all paths             %s", fails == 0 ? "PASS" : "FAIL" );
    }

    // Random subsets and cameras, including tie-heavy ones: cameras snapped
    // to chunk corners/centres make many chunk centres exactly equidistant.
    const glm::vec3 cameras[] = {
        glm::vec3( 0.0f ),                    // chunk corner: maximal ties
        glm::vec3( 8.0f ),                    // a chunk centre
        glm::vec3( 16.0f, 0.0f, -16.0f ),     // grid-snapped, off-axis
        glm::vec3( -4.5f, 2.5f, 4.5f ),       // half-block offsets
        glm::vec3( 1.3f, -2.7f, 0.4f ),       // arbitrary
        glm::vec3( 63.9f, -30.2f, 17.1f ),    // far corner of the grid
    };
    for ( const glm::vec3 &cam : cameras ) {
        for ( int rep = 0; rep < 3; rep++ ) {
            const int n = 1 + rand( ) % 300;
            std::vector<Chunk *> list = shuffled( all );
            list.resize( n );
            const std::vector<Chunk *> expected = reference( list, cam );

            // Spread of sorted_n: none, all, and in-between. The prefix is
            // pre-sorted for a camera near `cam` (the steady-state case) or
            // far from it under full_prefix (the teleport case).
            const int sorted_ns[] = { 0, n, n / 2, n > 4 ? n - 2 : n / 3 };
            for ( int s = 0; s < 4; s++ ) {
                const int sorted_n = sorted_ns[ s ];
                for ( int far = 0; far < 2; far++ ) {
                    // Near offset stays under CHUNK_SORT_RESORT_DIST
                    // (2 blocks); far offset is past CHUNK_SIZE_X.
                    const glm::vec3 prev_cam = cam + ( far ? glm::vec3( 64.0f, 0.0f, 0.0f ) : glm::vec3( 0.5f, 0.0f, 0.0f ) );
                    std::vector<Chunk *> order = list;
                    // Prefix in a previous sorted order, tail shuffled in.
                    std::vector<Chunk *> prefix( order.begin( ), order.begin( ) + sorted_n );
                    prefix = reference( prefix, prev_cam );
                    std::vector<Chunk *> tail = shuffled( std::vector<Chunk *>( order.begin( ) + sorted_n, order.end( ) ) );
                    std::vector<Chunk *> input = prefix;
                    input.insert( input.end( ), tail.begin( ), tail.end( ) );

                    sort_chunks_by_distance( input.data( ), n, sorted_n, ro, cam, far != 0, scratch );
                    const bool ok = input == expected;
                    fails += !ok;
                    if ( !ok ) {
                        pr_test( "  sort: cam=(%.1f,%.1f,%.1f) n=%d sorted_n=%d far=%d  FAIL", cam.x, cam.y, cam.z, n, sorted_n, far );
                    }
                }
            }
        }
    }
    pr_test( "  sort: merged prefix+tail matches reference order  %s", fails == 0 ? "PASS" : "FAIL" );
    return fails;
}
