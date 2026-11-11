// rep_tests coverage for the offline inspector (harness/inspect): persist a
// chunk through the real save path, then read it back through the inspect
// core functions (JSON strings — no stdout capture needed).

#include "test_util.hpp"

#include "common/chunk.hpp"
#include "common/harness/inspect.hpp"
#include "common/utils/file_utils.hpp"
#include "common/utils/map_storage.hpp"

#include <string.h>

static int check_has( const char *name, const std::string &hay, const char *needle ) {
    const bool ok = hay.find( needle ) != std::string::npos;
    pr_test( "  inspect: %-40s %s", name, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

int test_inspect( ) {
    int fails = 0;
    block_definitions_initilize_definitions( nullptr );

    // World pos (10,50,10) in chunk (0,0,0), local (10,50,10). TNT can't
    // appear in natural terrain, so it's a safe saved-edit marker.
    ScopedTempHome temp_home( "/tmp/repgame_test_inspect_home" );
    {
        MapStorage storage;
        storage.init( "TestInspect" );
        BlockState tnt{ TNT, 0, 0, TNT, 0 };
        std::vector<ChunkEditRun> runs{ { static_cast<uint32_t>( Chunk::get_index_from_coords( 10, 50, 10 ) ), 1u, tnt } };
        storage.persist_chunk_edits( glm::ivec3( 0, 0, 0 ), runs );
    }

    char world_dir[ 512 ];
    char *root = getRepGamePath( );
    snprintf( world_dir, sizeof( world_dir ), "%s%s%s", root, REPGAME_PATH_DIVIDOR, "TestInspect" );
    free( root );

    inspect::Result b = inspect::block( world_dir, 10, 50, 10 );
    fails += check_has( "block json name", b.json, "\"name\":\"TNT\"" );
    fails += check_has( "block saved", b.json, "\"saved\":true" );
    fails += check_has( "block edited", b.json, "\"edited\":true" );
    fails += check_has( "block chunk coords", b.json, "\"local\":[10,50,10]" );

    inspect::Result unedited = inspect::block( world_dir, 10, 49, 10 );
    fails += check_has( "unedited block", unedited.json, "\"edited\":false" );

    inspect::Result missing = inspect::block( world_dir, 5000, 5, 5000 );
    fails += check_has( "unsaved block", missing.json, "\"saved\":false" );
    fails += check_has( "unsaved not edited", missing.json, "\"edited\":false" );

    inspect::Result s = inspect::slice( world_dir, 50, 9, 9, 11, 11 );
    fails += check_has( "slice legend", s.json, "\"TNT\"" );

    inspect::Result c = inspect::chunk( world_dir, 0, 0, 0 );
    fails += check_has( "chunk counts", c.json, "\"TNT\":1" );
    fails += check_has( "chunk edited", c.json, "\"edited\":1" );
    fails += check_has( "chunk saved", c.json, "\"saved\":true" );

    inspect::Result col = inspect::column( world_dir, 10, 10, 49, 51 );
    fails += check_has( "column runs", col.json, "\"name\":\"TNT\"" );

    return fails;
}
