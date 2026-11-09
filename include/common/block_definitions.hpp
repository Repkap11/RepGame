#pragma once

// The block definition table and its accessors. The vocabulary types
// (Block, BlockState, BlockCell/BlockExtra, MineableClass, InventorySlot,
// cube-topology defines) live in common/block_types.hpp — include that
// directly instead of this header when you don't need the table.

#include <cstdint>

#include "common/block_types.hpp"
#include "Logging.hpp"

// Forward-declared: only block_definitions_initilize_definitions takes one.
class Texture;

// Applies the waterlogging rules for writing `state` into a cell currently
// holding `cur`. waterlogged is per-cell state (the cell holds water + the
// block); waterloggable is the per-type property saying the block may live
// inside water. NOT idempotent — each physical cell must be written through
// this at most once per logical set (World::set_loaded_block relies on that).
// Declared here (defined after `block_definition_get_definition` below).
static inline BlockState block_state_normalize( const BlockState &cur, BlockState state );

void block_definitions_initilize_definitions( Texture *texture );
void block_definitions_free_definitions( );

// Generated name tables (block_names_gen.inc / texture_names_gen.inc).
// Undefined ids return nullptr.
const char *block_definition_get_name( BlockID id );
const char *texture_name( TextureID id );

// Global block definition table (allocated in block_definitions.cpp).
extern Block *block_definitions;

// Inlined for hot meshing paths (called ~26x per block in calculate_populated_blocks).
static inline Block *block_definition_get_definition( BlockID blockID ) {
    if ( blockID < LAST_BLOCK_ID ) {
        return &block_definitions[ blockID ];
    } else {
        pr_debug( "Invalid block id:%d", blockID );
        return &block_definitions[ AIR ];
    }
}

static inline BlockState block_state_normalize( const BlockState &cur, BlockState state ) {
    if ( state.id == WATER ) {
        // Water placed/flowing into a waterloggable cell waterlogs the block
        // instead of replacing it.
        if ( block_definition_get_definition( cur.id )->waterloggable ) {
            state = cur;
            state.waterlogged = 1;
        }
    } else if ( block_definition_get_definition( state.id )->waterloggable ) {
        // A waterloggable block keeps water only if the cell held water.
        state.waterlogged = cur.id == WATER || cur.waterlogged;
    } else {
        if ( state.id == AIR && cur.waterlogged ) {
            // Breaking a waterlogged block leaves the water behind.
            state = { WATER, BLOCK_ROTATE_0, 0, WATER, 0 };
        }
        state.waterlogged = 0;
    }
    return state;
}
