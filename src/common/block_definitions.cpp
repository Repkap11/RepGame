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

    block_definitions[ DOUBLE_SLAB ].textures.top = SLAB_TOP;
    block_definitions[ DOUBLE_SLAB ].textures.side = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].textures.bottom = SLAB_TOP;

    block_definitions[ TNT ].textures.top = TNT_TOP;
    block_definitions[ TNT ].textures.side = TNT;
    block_definitions[ TNT ].textures.bottom = TNT_BOTTOM;

    block_definitions[ WOOD_LOG_TOP ].textures.top = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].textures.side = WOOD_LOG_SIDE;
    block_definitions[ WOOD_LOG_TOP ].textures.bottom = WOOD_LOG_TOP;

    block_definitions[ WATER ].renderOrder = RenderOrder_Water;
    block_definitions[ WHITE_GLASS ].renderOrder = RenderOrder_Glass;
    block_definitions[ LEAF ].renderOrder = RenderOrder_Leafs;


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