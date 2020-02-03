
#include "common/rep_tests.hpp"

#include "common/RepGame.hpp"

#include "common/mobs.hpp"
#include "common/perlin_noise.hpp"

typedef struct Test {
    const char *name;
    int ( *test_func )( );
} Test;

#define MK_TEST( name )                                                                                                                                                                                                                        \
    { #name, &test_##name }

int test_mobs( ) {
    pr_test( "Testing Mobs" );
    Mobs mobs;
    VertexBufferLayout vbl_object_vertex;
    VertexBufferLayout vbl_object_placement;
    vertex_buffer_layout_init( &vbl_object_vertex );
    vertex_buffer_layout_init( &vbl_object_placement );

    int inconsistent = 0;
    int num_test_mobs = 1000;

    mobs_init( &mobs, &vbl_object_vertex, &vbl_object_placement );
    for ( int mob_id = 0; mob_id < num_test_mobs; mob_id++ ) {
        mobs_add_mob( &mobs, mob_id );
        inconsistent |= mobs_check_consistency( &mobs );
    }

    for ( int mob_id = 0; mob_id < num_test_mobs; mob_id++ ) {
        float rand = perlin_noise( 0.0f, 0.0f, 1.0, 1, mob_id );
        if ( rand < 0.5f ) {
            mobs_remove_mob( &mobs, mob_id );
            inconsistent |= mobs_check_consistency( &mobs );
        }
    }
    for ( int mob_id = 0; mob_id < num_test_mobs; mob_id++ ) {
        float rand = perlin_noise( 100.0f, 0.0f, 1.0, 1, mob_id );
        if ( rand < 0.5f ) {
            mobs_remove_mob( &mobs, mob_id );
            inconsistent |= mobs_check_consistency( &mobs );
        }
    }
    inconsistent |= mobs_check_consistency( &mobs );

    return inconsistent;
}

const Test all_tests[] = { //
    MK_TEST( mobs ),       //
    {NULL, NULL}

};

int rep_tests_start( ) {
    pr_test( "\nStarting Rep Tests" );

    int i = 0;
    int failed = 0;
    int another_test = 1;
    do {
        const Test *test = &all_tests[ i ];
        i++;
        another_test = test->name && test->test_func;
        if ( another_test ) {
            int result = test->test_func( );
            if ( result ) {
                failed = 1;
                pr_test( "Test \"%s\" failed with %d", test->name, result );
            } else {
                pr_test( "Test \"%s\" passed", test->name );
            }
        }
    } while ( another_test );
    if ( failed ) {
        pr_test( "A Rep Tests Failed!!!\n" );
    } else {
        pr_test( "All Rep Tests passed\n" );
    }
    return failed;
}