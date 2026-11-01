#include "common/item.hpp"

static const char *ITEM_NAMES[ ITEM_ID_END - ITEM_ID_BASE ] = {
#include "item_names_gen.inc"
};

TextureID item_icon_texture( ItemID id ) {
    if ( item_is_block( id ) ) {
        return block_definition_get_definition( static_cast<BlockID>( id ) )->icon_texture;
    }
    return item_id_defined( id ) ? ITEM_DEFINITIONS[ id - ITEM_ID_BASE ].icon_texture : 0;
}

int item_icon_is_isometric( ItemID id ) {
    if ( item_is_block( id ) ) {
        return block_definition_get_definition( static_cast<BlockID>( id ) )->icon_is_isometric;
    }
    return 0;
}

const char *item_name( ItemID id ) {
    if ( item_is_block( id ) ) {
        return block_definition_get_name( static_cast<BlockID>( id ) );
    }
    return item_id_defined( id ) ? ITEM_NAMES[ id - ITEM_ID_BASE ] : nullptr;
}

#include "block_drops_gen.inc"

int block_roll_drops( BlockID id, uint32_t *rng_state, ItemStack *out, int max_out ) {
    // BLOCK_DROP_TABLE is sorted by block id — early exit past the match.
    const int table_len = sizeof( BLOCK_DROP_TABLE ) / sizeof( BLOCK_DROP_TABLE[ 0 ] );
    int entry_offset = -1;
    int entry_count = 0;
    for ( int i = 0; i < table_len; i++ ) {
        if ( BLOCK_DROP_TABLE[ i ].block_id == id ) {
            entry_offset = BLOCK_DROP_TABLE[ i ].offset;
            entry_count = BLOCK_DROP_TABLE[ i ].count;
            break;
        }
        if ( BLOCK_DROP_TABLE[ i ].block_id > id ) {
            break;
        }
    }
    if ( entry_offset < 0 ) {
        // No "drops" key: the block drops itself.
        if ( max_out > 0 ) {
            out[ 0 ].item_id = id;
            out[ 0 ].count = 1;
        }
        return max_out > 0 ? 1 : 0;
    }
    int written = 0;
    // Entries with chance < 1 are mutually exclusive: one roll picks the first
    // entry whose cumulative chance covers it; a shortfall past the total
    // drops none of them. Entries with chance == 1 always drop.
    const float exclusive_roll = static_cast<float>( item_rng_next( rng_state ) ) / 4294967296.0f;
    float cumulative = 0.0f;
    int exclusive_winner = -1;
    for ( int i = 0; i < entry_count; i++ ) {
        const BlockDropGen &d = BLOCK_DROP_ENTRIES[ entry_offset + i ];
        if ( d.chance >= 1.0f ) {
            continue;
        }
        cumulative += d.chance;
        if ( exclusive_winner < 0 && exclusive_roll < cumulative ) {
            exclusive_winner = i;
        }
    }
    for ( int i = 0; i < entry_count && written < max_out; i++ ) {
        const BlockDropGen &d = BLOCK_DROP_ENTRIES[ entry_offset + i ];
        if ( d.chance < 1.0f && i != exclusive_winner ) {
            continue;
        }
        const int count = d.min + static_cast<int>( item_rng_next( rng_state ) % static_cast<uint32_t>( d.max - d.min + 1 ) );
        if ( count <= 0 ) {
            continue;
        }
        out[ written ].item_id = d.item_id;
        out[ written ].count = count;
        written++;
    }
    return written;
}
