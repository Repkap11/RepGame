#include "test_util.hpp"

void test_setup_world( World &world ) {
    ChunkLoader &cl = world.chunkLoader;
    cl.chunkArray = new Chunk[ MAX_LOADED_CHUNKS ]( );
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        cl.chunkArray[ i ].light.stale_word = &cl.light_stale_bits[ i >> 6 ];
        cl.chunkArray[ i ].light.stale_bit = 1ull << ( i & 63 );
    }
    Chunk &chunk = cl.chunkArray[ chunk_slot_from_pos( glm::ivec3( 0, 0, 0 ) ) ];
    chunk.chunk_pos = glm::ivec3( 0, 0, 0 );
    chunk.chunk_mod = glm::ivec3( 0, 0, 0 );
    chunk.load_state = CHUNK_LIVE;
    chunk.cells = chunk_alloc_cells( );
}

int check_power( const World &world, const char *name, const glm::ivec3 &pos, int expected, int expected_max ) {
    const int power = world.get_loaded_block( pos ).current_redstone_power;
    const bool ok = expected_max < 0 ? power == expected : ( power >= expected && power <= expected_max );
    pr_test( "  %-55s power=%-2d expected=%s%d  %s", name, power, expected_max < 0 ? "" : ">=", expected, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

int check_id( const World &world, const char *name, const glm::ivec3 &pos, BlockID expected ) {
    const BlockID id = world.get_loaded_block( pos ).id;
    const bool ok = id == expected;
    pr_test( "  %-55s id=%-3d expected=%-3d  %s", name, id, expected, ok ? "PASS" : "FAIL" );
    return ok ? 0 : 1;
}

void test_setup_inventories( RepGameState &gs ) {
    gs.hotbar.width = HOTBAR_WIDTH;
    gs.hotbar.height = HOTBAR_HEIGHT;
    gs.hotbar.num_blocks_max = HOTBAR_WIDTH * HOTBAR_HEIGHT;
    gs.hotbar.slots = static_cast<InventorySlot *>( calloc( gs.hotbar.num_blocks_max, sizeof( InventorySlot ) ) );
    for ( int i = 0; i < gs.hotbar.num_blocks_max; i++ ) {
        gs.hotbar.slots[ i ].item_id = LAST_BLOCK_ID;
        gs.hotbar.slots[ i ].quantity = 0;
    }
    gs.hotbar.selected_slot = 0;
    InventoryRenderer &ir = gs.hotbar.inventory_renderer;
    ir.width = HOTBAR_WIDTH;
    ir.height = HOTBAR_HEIGHT;
    ir.num_blocks_max = HOTBAR_WIDTH * HOTBAR_HEIGHT;
    ir.screen_width = 800;
    ir.screen_height = 600;
    ir.inv_items_x = 0;
    ir.inv_items_y = 0;
    ir.inv_cell_stride = 1.0f;
    ir.inv_block_size = 1;
    ir.inv_block_offset = 0;
    ir.slots_entities = static_cast<entt::entity *>( calloc( ir.num_blocks_max * ICON_MAX_BOXES, sizeof( entt::entity ) ) );
    for ( int i = 0; i < ir.num_blocks_max * ICON_MAX_BOXES; i++ ) {
        ir.slots_entities[ i ] = entt::null;
    }

    SurvivalInventory &si = gs.survival_inventory;
    si.width = SURVIVAL_INVENTORY_WIDTH;
    si.height = SURVIVAL_INVENTORY_HEIGHT;
    si.num_slots = SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT;
    si.slots = static_cast<InventorySlot *>( calloc( si.num_slots, sizeof( InventorySlot ) ) );
    for ( int i = 0; i < si.num_slots; i++ ) {
        si.slots[ i ].item_id = LAST_BLOCK_ID;
        si.slots[ i ].quantity = 0;
    }
    InventoryRenderer &sir = si.inventory_renderer;
    sir.width = SURVIVAL_INVENTORY_WIDTH;
    sir.height = SURVIVAL_INVENTORY_HEIGHT;
    sir.num_blocks_max = si.num_slots;
    sir.screen_width = 800;
    sir.screen_height = 600;
    sir.inv_items_x = 0;
    sir.inv_items_y = 0;
    sir.inv_cell_stride = 1.0f;
    sir.inv_block_size = 1;
    sir.inv_block_offset = 0;
    sir.slots_entities = static_cast<entt::entity *>( calloc( sir.num_blocks_max * ICON_MAX_BOXES, sizeof( entt::entity ) ) );
    for ( int i = 0; i < sir.num_blocks_max * ICON_MAX_BOXES; i++ ) {
        sir.slots_entities[ i ] = entt::null;
    }
}

ScopedTempHome::ScopedTempHome( const char *dir ) {
    const char *orig = getenv( "HOME" );
    this->had_home = orig != nullptr;
    this->orig_home[ 0 ] = '\0';
    if ( orig != nullptr ) {
        strncpy( this->orig_home, orig, sizeof( this->orig_home ) - 1 );
    }
    setenv( "HOME", dir, 1 );
}

ScopedTempHome::~ScopedTempHome( ) {
    if ( this->had_home ) {
        setenv( "HOME", this->orig_home, 1 );
    } else {
        unsetenv( "HOME" );
    }
}
