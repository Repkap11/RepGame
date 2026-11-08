// rep_tests coverage for the light_stale per-slot atomic bitset
// (ChunkLoader::light_stale_bits + Chunk::light_stale_word/_bit helpers):
// set/get/take/clear semantics, idempotent set, take-once, word/bit mapping
// at word boundaries and the last slot, unbound-chunk no-ops, and that the
// loader-stats popcount equals the number of flagged slots.

#include "test_util.hpp" // IWYU pragma: keep — shared umbrella for all rep_tests TUs

int test_light_stale( ) {
    int fails = 0;

    ChunkLoader cl;
    cl.chunkArray = new Chunk[ MAX_LOADED_CHUNKS ]( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        cl.chunkArray[ i ].light_stale_word = &cl.light_stale_bits[ i >> 6 ];
        cl.chunkArray[ i ].light_stale_bit = 1ull << ( i & 63 );
    }

    Chunk &c63 = cl.chunkArray[ 63 ];
    Chunk &c64 = cl.chunkArray[ 64 ];
    Chunk &clast = cl.chunkArray[ MAX_LOADED_CHUNKS - 1 ];

    // set/get/take/clear basics + idempotent set + take returns true once.
    c63.light_stale_set( );
    fails += !c63.light_stale_get( );
    c63.light_stale_set( ); // idempotent: still one bit, neighbour untouched
    fails += !c63.light_stale_get( );
    fails += ( cl.light_stale_bits[ 0 ].load( ) & ~ ( 1ull << 63 ) ) != 0;
    fails += !c63.light_stale_take( );
    fails += c63.light_stale_get( );
    fails += c63.light_stale_take( ); // second take must fail
    pr_test( "  stale: set/get/take once, idempotent set          %s", fails == 0 ? "PASS" : "FAIL" );

    // Word-boundary mapping: slot 63 -> word 0 bit 63, slot 64 -> word 1 bit 0.
    c63.light_stale_set( );
    c64.light_stale_set( );
    clast.light_stale_set( );
    fails += cl.light_stale_bits[ 0 ].load( ) != ( 1ull << 63 );
    fails += cl.light_stale_bits[ 1 ].load( ) != 1ull;
    fails += cl.light_stale_bits[ ChunkLoader::LIGHT_STALE_WORDS - 1 ].load( ) != ( 1ull << ( ( MAX_LOADED_CHUNKS - 1 ) & 63 ) );
    // Clearing slot 64 must not disturb 63 or the last slot.
    c64.light_stale_clear( );
    fails += !c63.light_stale_get( ) || !clast.light_stale_get( );
    fails += c64.light_stale_get( );
    c63.light_stale_clear( );
    clast.light_stale_clear( );
    pr_test( "  stale: word/bit mapping at 63/64/last boundaries  %s", fails == 0 ? "PASS" : "FAIL" );

    // Unbound chunk: all ops no-op / read false.
    Chunk unbound{ };
    fails += unbound.light_stale_get( );
    fails += unbound.light_stale_take( );
    unbound.light_stale_set( );
    unbound.light_stale_clear( );
    fails += unbound.light_stale_get( );
    pr_test( "  stale: unbound chunk no-ops                       %s", fails == 0 ? "PASS" : "FAIL" );

    // Popcount over the words equals the number of flagged slots.
    int want = 0;
    const int flagged[] = { 0, 5, 63, 64, 127, 4096, MAX_LOADED_CHUNKS - 1 };
    for ( int i : flagged ) {
        cl.chunkArray[ i ].light_stale_set( );
        want++;
    }
    int got = 0;
    for ( int w = 0; w < ChunkLoader::LIGHT_STALE_WORDS; w++ ) {
        got += __builtin_popcountll( cl.light_stale_bits[ w ].load( ) );
    }
    fails += got != want;
    pr_test( "  stale: popcount equals flagged slot count         %s", fails == 0 ? "PASS" : "FAIL" );

    delete[] cl.chunkArray;
    return fails;
}
