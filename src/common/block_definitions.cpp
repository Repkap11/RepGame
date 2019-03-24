#include "common/block_definitions.hpp"
#include "common/abstract/textures.hpp"
#include "common/RepGame.hpp"
#include <stdlib.h>

Block *block_definitions;

void block_definitions_initilize_definitions( Texture *texture ) {
    block_definitions = ( Block * )calloc( LAST_BLOCK_ID, sizeof( Block ) );
    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];
        block->id = ( BlockID )block_id;
        block->renderOrder = RenderOrder_Solid;
        block->height = 1.0f;
        block->textures.top = ( BlockID )block_id;
        block->textures.side = ( BlockID )block_id;
        block->textures.bottom = ( BlockID )block_id;
    }

    block_definitions[ AIR ].renderOrder = RenderOrder_Transparent;

    block_definitions[ GRASS ].textures.top = GRASS;
    block_definitions[ GRASS ].textures.side = GRASS_SIDE;
    block_definitions[ GRASS ].textures.bottom = DIRT;
    block_definitions[ GRASS_SIDE ].renderOrder = RenderOrder_Transparent;

    block_definitions[ DOUBLE_SLAB ].textures.top = SLAB_TOP;
    block_definitions[ DOUBLE_SLAB ].textures.side = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].textures.bottom = SLAB_TOP;
    block_definitions[ SLAB_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ TNT ].textures.top = TNT_TOP;
    block_definitions[ TNT ].textures.side = TNT;
    block_definitions[ TNT ].textures.bottom = TNT_BOTTOM;
    block_definitions[ TNT_BOTTOM ].renderOrder = RenderOrder_Transparent;
    block_definitions[ TNT_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ WOOD_LOG_SIDE ].textures.top = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_SIDE ].textures.side = WOOD_LOG_SIDE;
    block_definitions[ WOOD_LOG_SIDE ].textures.bottom = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ WATER ].renderOrder = RenderOrder_Water;
    block_definitions[ WHITE_GLASS ].renderOrder = RenderOrder_Glass;
    block_definitions[ LEAF ].renderOrder = RenderOrder_Leafs;

    block_definitions[ BOOK_CASE ].textures.top = OAK_PLANK;
    block_definitions[ BOOK_CASE ].textures.bottom = OAK_PLANK;

    block_definitions[ CRAFTING_BENCH ].textures.side = CRAFTING_BENCH_SIDE;
    block_definitions[ CRAFTING_BENCH ].textures.bottom = OAK_PLANK;
    block_definitions[ CRAFTING_BENCH_SIDE ].renderOrder = RenderOrder_Transparent;

    block_definitions[ SINGLE_CHEST_SIDE ].textures.top = SINGLE_CHEST_TOP;
    block_definitions[ SINGLE_CHEST_SIDE ].textures.bottom = SINGLE_CHEST_TOP;
    block_definitions[ SINGLE_CHEST_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ SNOWY_GRASS ].textures.top = SNOW;
    block_definitions[ SNOWY_GRASS ].textures.bottom = DIRT;

    block_definitions[ FURNACE_UNLIT ].textures.top = FURNACE_TOP;
    block_definitions[ FURNACE_UNLIT ].textures.bottom = FURNACE_TOP;
    block_definitions[ FURNACE_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ FURNACE_LIT ].textures.top = FURNACE_TOP;
    block_definitions[ FURNACE_LIT ].textures.bottom = FURNACE_TOP;

    block_definitions[ MYCELIUM ].textures.side = MYCELIUM_SIDE;
    block_definitions[ MYCELIUM ].textures.bottom = DIRT;
    block_definitions[ MYCELIUM_SIDE ].renderOrder = RenderOrder_Transparent;
}

Block *block_definition_get_definition( BlockID blockID ) {
    if ( blockID < LAST_BLOCK_ID ) {
        return &block_definitions[ blockID ];
    } else {
        pr_debug( "Invalid block id:%d", blockID );
        return &block_definitions[ AIR ];
    }
}

void block_definitions_free_definitions( ) {
    free( block_definitions );
}