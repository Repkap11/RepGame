// rep_tests runner: the explicit test table + rep_tests_start. Test bodies
// live one group per file under src/common/tests/ and are declared in
// test_list.hpp; the table order here is the run order.

#include "common/rep_tests.hpp"
#include "common/Logging.hpp"

#include "test_list.hpp"

#include <string.h>

typedef struct Test {
    const char *name;
    int ( *test_func )( );
} Test;

#define MK_TEST( name ) { #name, &test_##name }

// ecs is defined in test_core.cpp but deliberately left out of the table
// (see commit 512c08d).
constexpr Test all_tests[] = {  //
    MK_TEST( vr_projection ),   //
    MK_TEST( min_ambient_step ), //
    MK_TEST( angle_wrap ),       //
    MK_TEST( teleport_parse ),   //
    MK_TEST( redstone ),        //
    MK_TEST( torch_pick ),      //
    MK_TEST( ray_face_neighbor ), //
    MK_TEST( player_placement_valid ), //
    MK_TEST( sneak ),           //
    MK_TEST( climb ),           //
    MK_TEST( held_swing ),       //
    MK_TEST( items ),          //
    MK_TEST( furnace ),        //
    MK_TEST( item_entities ),  //
    MK_TEST( save_migration ),  //
    MK_TEST( player_data ),     //
    MK_TEST( creative ),        //
    MK_TEST( fences ),          //
    MK_TEST( stairs ),          //
    MK_TEST( slabs ),           //
    MK_TEST( cake ),            //
    MK_TEST( doors ),           //
    MK_TEST( block_world ),     //
    MK_TEST( mapgen ),          //
    MK_TEST( waterlog ),        //
    MK_TEST( fluids ),          //
    MK_TEST( lighting ),        //
    MK_TEST( tnt ),             //
    MK_TEST( falling_blocks ),  //
    MK_TEST( structure_border ), //
    MK_TEST( particles ),        //
    MK_TEST( mobs ),             //
    MK_TEST( net_codec_registry ), //
    MK_TEST( net_containers ),  //
    MK_TEST( net_entities ),     //
    MK_TEST( net_item_crossing ), //
    MK_TEST( net_item_grant ),   //
    MK_TEST( net_item_mirror_visuals ), //
    MK_TEST( net_mobs ),          //
    MK_TEST( net_bodies ),         //
    MK_TEST( arrows ),             //
    MK_TEST( net_multiplayer_mode ), //
    MK_TEST( crafting ),         //
    MK_TEST( results ),          //
    MK_TEST( script ),          //
    MK_TEST( inspect ),          //
    MK_TEST( profile ),          //
    MK_TEST( chunk_sort ),       //
    MK_TEST( chunk_occlusion ),  //
    MK_TEST( light_stale ),      //
    MK_TEST( leaf_decay ),       //
    MK_TEST( support_drops ),    //
    MK_TEST( sapling_grow ),     //
    MK_TEST( leaf_placement ),   //
    MK_TEST( shears_bonemeal ),  //
    MK_TEST( farming ),          //
    MK_TEST( grass ),            //
    MK_TEST( grass_persist ),    //
    { nullptr, nullptr }

};

// Runs every test whose name contains any argv filter as a substring; zero
// filters runs the whole table.
int rep_tests_start( int argc, const char *const *argv ) {
    pr_test( "\nStarting Rep Tests" );
    if ( argc > 0 ) {
        pr_test( "Filter(s):" );
        for ( int i = 0; i < argc; i++ ) {
            pr_test( "  %s", argv[ i ] );
        }
    }

    int run_count = 0;
    int fail_count = 0;
    const char *failed[ sizeof( all_tests ) / sizeof( all_tests[ 0 ] ) ];
    for ( const Test *test = all_tests; test->name != nullptr && test->test_func != nullptr; test++ ) {
        if ( argc > 0 ) {
            bool selected = false;
            for ( int i = 0; i < argc; i++ ) {
                if ( strstr( test->name, argv[ i ] ) != nullptr ) {
                    selected = true;
                    break;
                }
            }
            if ( !selected ) {
                continue;
            }
        }
        run_count++;
        if ( const int result = test->test_func( ) ) {
            failed[ fail_count++ ] = test->name;
            pr_test( "Test \"%s\" failed with %d", test->name, result );
        } else {
            pr_test( "Test \"%s\" passed", test->name );
        }
    }
    if ( run_count == 0 ) {
        pr_test( "No tests matched the filter\n" );
        return 1;
    }
    if ( fail_count > 0 ) {
        pr_test( "%d of %d test(s) failed:", fail_count, run_count );
        for ( int i = 0; i < fail_count; i++ ) {
            pr_test( "  %s", failed[ i ] );
        }
        pr_test( "A Rep Tests Failed!!!\n" );
    } else {
        pr_test( "All %d Rep Tests passed\n", run_count );
    }
    return fail_count > 0 ? 1 : 0;
}
