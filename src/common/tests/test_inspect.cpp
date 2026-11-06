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

    // World pos (10,1,10) in chunk (0,0,0), local (10,1,10).
    ScopedTempHome temp_home( "/tmp/repgame_test_inspect_home" );
    {
        MapStorage storage;
        storage.init( "TestInspect" );
        std::vector<BlockState> cells( CHUNK_BLOCK_SIZE, BLOCK_STATE_AIR );
        BlockState stone{ STONE, 0, 0, STONE, 0 };
        cells[ Chunk::get_index_from_coords( 10, 1, 10 ) ] = stone;
        storage.persist_dirty_blocks( glm::ivec3( 0, 0, 0 ), cells.data( ) );
    }

    char world_dir[ 512 ];
    char *root = getRepGamePath( );
    snprintf( world_dir, sizeof( world_dir ), "%s%s%s", root, REPGAME_PATH_DIVIDOR, "TestInspect" );
    free( root );

    inspect::Result b = inspect::block( world_dir, 10, 1, 10 );
    fails += check_has( "block json id", b.json, "\"id\":2" );
    fails += check_has( "block json name", b.json, "\"name\":\"STONE\"" );
    fails += check_has( "block saved", b.json, "\"saved\":true" );
    fails += check_has( "block chunk coords", b.json, "\"local\":[10,1,10]" );

    inspect::Result missing = inspect::block( world_dir, 5000, 5, 5000 );
    fails += check_has( "unsaved block", missing.json, "\"saved\":false" );
    fails += check_has( "unsaved id null", missing.json, "\"id\":null" );

    inspect::Result s = inspect::slice( world_dir, 1, 9, 9, 11, 11 );
    fails += check_has( "slice legend", s.json, "\"a\":\"STONE\"" );

    inspect::Result c = inspect::chunk( world_dir, 0, 0, 0 );
    fails += check_has( "chunk counts", c.json, "\"STONE\":1" );
    fails += check_has( "chunk saved", c.json, "\"saved\":true" );

    inspect::Result col = inspect::column( world_dir, 10, 10, 0, 2 );
    fails += check_has( "column runs", col.json, "\"name\":\"STONE\"" );

    return fails;
}
