#include "block_definitions.h"
#include "textures.h"
#include <stdlib.h>

BlockDefinition *block_definitions;
void block_definitions_initilize_definitions( ) {
    block_definitions = calloc( LAST_BLOCK_ID, sizeof( BlockDefinition ) );
    block_definitions[ AIR ].id = AIR;
    block_definitions[ AIR ].alpha = 0.0f;
    block_definitions[ AIR ].height = 0.0f;
    block_definitions[ AIR ].textures.top = 0;
    block_definitions[ AIR ].textures.side = 0;
    block_definitions[ AIR ].textures.bottom = 0;

    block_definitions[ GRASS ].id = GRASS;
    block_definitions[ GRASS ].alpha = 1.0f;
    block_definitions[ GRASS ].height = 1.0f;
    block_definitions[ GRASS ].textures.top = textures_getGrassTexture( );
    block_definitions[ GRASS ].textures.side = textures_getGrassSideTexture( );
    block_definitions[ GRASS ].textures.bottom = textures_getDirtTexture( );
    block_definitions[ GRASS ].special_grass_logic = 1;

    block_definitions[ WATER ].id = WATER;
    block_definitions[ WATER ].alpha = 0.5f;
    block_definitions[ WATER ].height = 14.0f / 16.0f;
    block_definitions[ WATER ].textures.top = textures_getWaterTexture( );
    block_definitions[ WATER ].textures.side = textures_getWaterTexture( );
    block_definitions[ WATER ].textures.bottom = textures_getWaterTexture( );
    block_definitions[ WATER ].special_grass_logic = 0;
}

BlockDefinition *block_definition_get_definition( BlockID blockID ) {
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