// Container kind table + the conversions between the generic
// ContainerContents blob and the per-kind storage shapes (ChestContents /
// FurnaceContents file IO, furnace ticking). See container.hpp.

#include "common/container.hpp"

#include "common/item.hpp"
#include "common/smelting.hpp"
#include "common/utils/map_storage.hpp"

// --- ChestContents <-> ContainerContents -----------------------------------

ContainerContents container_to_blob( const ChestContents &c ) {
    ContainerContents out;
    out.slots.assign( c.slots, c.slots + CHEST_SLOTS );
    return out;
}

void container_from_blob( const ContainerContents &c, ChestContents &out ) {
    for ( int i = 0; i < CHEST_SLOTS; i++ ) {
        out.slots[ i ] = i < static_cast<int>( c.slots.size( ) ) ? c.slots[ i ] : InventorySlot{ LAST_BLOCK_ID, 0 };
    }
}

static void chest_load( MapStorage &storage, const glm::ivec3 &chunk_pos, ContainerContentsMap &out ) {
    ChestContentsMap typed;
    storage.load_chests( chunk_pos, typed );
    for ( auto &pair : typed ) {
        out[ pair.first ] = container_to_blob( pair.second );
    }
}

static void chest_persist( MapStorage &storage, const glm::ivec3 &chunk_pos, const ContainerContentsMap &contents ) {
    ChestContentsMap typed;
    for ( const auto &pair : contents ) {
        container_from_blob( pair.second, typed[ pair.first ] );
    }
    storage.persist_chests( chunk_pos, typed );
}

// --- FurnaceContents <-> ContainerContents ---------------------------------
// aux[0] = cook_progress, aux[1] = burn_ticks.

ContainerContents container_to_blob( const FurnaceContents &f ) {
    ContainerContents out;
    out.slots.assign( f.slots, f.slots + FURNACE_INVENTORY_SLOTS );
    out.aux.assign( { f.cook_progress, f.burn_ticks } );
    return out;
}

void container_from_blob( const ContainerContents &c, FurnaceContents &out ) {
    for ( int i = 0; i < FURNACE_INVENTORY_SLOTS; i++ ) {
        out.slots[ i ] = i < static_cast<int>( c.slots.size( ) ) ? c.slots[ i ] : InventorySlot{ LAST_BLOCK_ID, 0 };
    }
    out.cook_progress = c.aux.size( ) > 0 ? c.aux[ 0 ] : 0;
    out.burn_ticks = c.aux.size( ) > 1 ? c.aux[ 1 ] : 0;
}

static void furnace_load( MapStorage &storage, const glm::ivec3 &chunk_pos, ContainerContentsMap &out ) {
    FurnaceContentsMap typed;
    storage.load_furnaces( chunk_pos, typed );
    for ( auto &pair : typed ) {
        out[ pair.first ] = container_to_blob( pair.second );
    }
}

static void furnace_persist( MapStorage &storage, const glm::ivec3 &chunk_pos, const ContainerContentsMap &contents ) {
    FurnaceContentsMap typed;
    for ( const auto &pair : contents ) {
        container_from_blob( pair.second, typed[ pair.first ] );
    }
    storage.persist_furnaces( chunk_pos, typed );
}

static bool furnace_active( const ContainerContents &contents ) {
    FurnaceContents f;
    container_from_blob( contents, f );
    if ( f.burn_ticks > 0 ) {
        return true;
    }
    const InventorySlot &input = f.slots[ FURNACE_SLOT_INPUT ];
    const bool has_work = input.item_id != LAST_BLOCK_ID && smelt_result_item( input.item_id ) != LAST_BLOCK_ID;
    const InventorySlot &fuel = f.slots[ FURNACE_SLOT_FUEL ];
    return has_work && fuel.item_id != LAST_BLOCK_ID && fuel.quantity > 0 && furnace_fuel_ticks( fuel.item_id ) > 0;
}

static bool furnace_tick_blob( ContainerContents &contents, bool *lit_changed, bool *changed ) {
    FurnaceContents f;
    container_from_blob( contents, f );
    const bool ret = furnace_tick( f, changed );
    if ( ret || *changed ) {
        contents = container_to_blob( f );
    }
    *lit_changed = ret;
    return ret;
}

static bool furnace_lit( const ContainerContents &contents ) {
    // aux[1] = burn_ticks.
    return contents.aux.size( ) > 1 && contents.aux[ 1 ] > 0;
}

// ---------------------------------------------------------------------------

static const NetContainerKindDef kKindDefs[ NET_CONTAINER_KIND_COUNT ] = {
    {
        NetContainerKind::CHEST,
        "chest",
        CHEST_SLOTS,
        0,
        block_is_chest,
        chest_load,
        chest_persist,
        nullptr,
        nullptr,
        nullptr,
        static_cast<BlockID>( LAST_BLOCK_ID ),
        static_cast<BlockID>( LAST_BLOCK_ID ),
    },
    {
        NetContainerKind::FURNACE,
        "furnace",
        FURNACE_INVENTORY_SLOTS,
        2,
        block_is_furnace,
        furnace_load,
        furnace_persist,
        furnace_active,
        furnace_tick_blob,
        furnace_lit,
        FURNACE,
        LIT_FURNACE,
    },
};

const NetContainerKindDef *net_container_kind_def( uint8_t kind ) {
    if ( kind >= NET_CONTAINER_KIND_COUNT ) {
        return nullptr;
    }
    return &kKindDefs[ kind ];
}

const NetContainerKindDef *net_container_kind_of_block( BlockID id ) {
    for ( const NetContainerKindDef &def : kKindDefs ) {
        if ( def.block_is( id ) ) {
            return &def;
        }
    }
    return nullptr;
}

InventorySlot container_clean_slot( InventorySlot slot ) {
    if ( !item_id_valid( slot.item_id ) || slot.quantity <= 0 ) {
        slot.item_id = LAST_BLOCK_ID;
        slot.quantity = 0;
    } else if ( slot.quantity > item_max_stack( slot.item_id ) ) {
        slot.quantity = item_max_stack( slot.item_id );
    }
    return slot;
}
