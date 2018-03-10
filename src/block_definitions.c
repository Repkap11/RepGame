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
    block_definitions[ GRASS ].textures.top = getTexture( GRASS );
    block_definitions[ GRASS ].textures.side = getTexture( GRASS_SIDE );
    block_definitions[ GRASS ].textures.bottom = getTexture( DIRT );
    block_definitions[ GRASS ].special_grass_logic = 1;

    block_definitions[ STONE ].id = STONE;
    block_definitions[ STONE ].alpha = 1.0f;
    block_definitions[ STONE ].height = 1.0f;
    block_definitions[ STONE ].textures.top = getTexture( STONE );
    block_definitions[ STONE ].textures.side = getTexture( STONE );
    block_definitions[ STONE ].textures.bottom = getTexture( STONE );
    block_definitions[ STONE ].special_grass_logic = 0;

    block_definitions[ DIRT ].id = DIRT;
    block_definitions[ DIRT ].alpha = 1.0f;
    block_definitions[ DIRT ].height = 1.0f;
    block_definitions[ DIRT ].textures.top = getTexture( DIRT );
    block_definitions[ DIRT ].textures.side = getTexture( DIRT );
    block_definitions[ DIRT ].textures.bottom = getTexture( DIRT );
    block_definitions[ DIRT ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( WOOD_PLANK );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( WOOD_PLANK );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( WOOD_PLANK );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ DOUBLE_SLAB ].id = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].alpha = 1.0f;
    block_definitions[ DOUBLE_SLAB ].height = 1.0f;
    block_definitions[ DOUBLE_SLAB ].textures.top = getTexture( SLAB_TOP );
    block_definitions[ DOUBLE_SLAB ].textures.side = getTexture( DOUBLE_SLAB );
    block_definitions[ DOUBLE_SLAB ].textures.bottom = getTexture( SLAB_TOP );
    block_definitions[ DOUBLE_SLAB ].special_grass_logic = 0;

    block_definitions[ BRICK ].id = BRICK;
    block_definitions[ BRICK ].alpha = 1.0f;
    block_definitions[ BRICK ].height = 1.0f;
    block_definitions[ BRICK ].textures.top = getTexture( BRICK );
    block_definitions[ BRICK ].textures.side = getTexture( BRICK );
    block_definitions[ BRICK ].textures.bottom = getTexture( BRICK );
    block_definitions[ BRICK ].special_grass_logic = 0;

    block_definitions[ TNT ].id = TNT;
    block_definitions[ TNT ].alpha = 1.0f;
    block_definitions[ TNT ].height = 1.0f;
    block_definitions[ TNT ].textures.top = getTexture( TNT_TOP );
    block_definitions[ TNT ].textures.side = getTexture( TNT );
    block_definitions[ TNT ].textures.bottom = getTexture( TNT_BOTTOM );
    block_definitions[ TNT ].special_grass_logic = 0;

    block_definitions[ COBBLESTONE ].id = COBBLESTONE;
    block_definitions[ COBBLESTONE ].alpha = 1.0f;
    block_definitions[ COBBLESTONE ].height = 1.0f;
    block_definitions[ COBBLESTONE ].textures.top = getTexture( COBBLESTONE );
    block_definitions[ COBBLESTONE ].textures.side = getTexture( COBBLESTONE );
    block_definitions[ COBBLESTONE ].textures.bottom = getTexture( COBBLESTONE );
    block_definitions[ COBBLESTONE ].special_grass_logic = 0;

    block_definitions[ BEDROCK ].id = BEDROCK;
    block_definitions[ BEDROCK ].alpha = 1.0f;
    block_definitions[ BEDROCK ].height = 1.0f;
    block_definitions[ BEDROCK ].textures.top = getTexture( BEDROCK );
    block_definitions[ BEDROCK ].textures.side = getTexture( BEDROCK );
    block_definitions[ BEDROCK ].textures.bottom = getTexture( BEDROCK );
    block_definitions[ BEDROCK ].special_grass_logic = 0;

    block_definitions[ GRAVEL ].id = GRAVEL;
    block_definitions[ GRAVEL ].alpha = 1.0f;
    block_definitions[ GRAVEL ].height = 1.0f;
    block_definitions[ GRAVEL ].textures.top = getTexture( GRAVEL );
    block_definitions[ GRAVEL ].textures.side = getTexture( GRAVEL );
    block_definitions[ GRAVEL ].textures.bottom = getTexture( GRAVEL );
    block_definitions[ GRAVEL ].special_grass_logic = 0;

    block_definitions[ WOOD_LOG_TOP ].id = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].alpha = 1.0f;
    block_definitions[ WOOD_LOG_TOP ].height = 1.0f;
    block_definitions[ WOOD_LOG_TOP ].textures.top = getTexture( WOOD_LOG_TOP );
    block_definitions[ WOOD_LOG_TOP ].textures.side = getTexture( WOOD_LOG_SIDE );
    block_definitions[ WOOD_LOG_TOP ].textures.bottom = getTexture( WOOD_LOG_TOP );
    block_definitions[ WOOD_LOG_TOP ].special_grass_logic = 0;

    block_definitions[ IRON_BLOCK ].id = IRON_BLOCK;
    block_definitions[ IRON_BLOCK ].alpha = 1.0f;
    block_definitions[ IRON_BLOCK ].height = 1.0f;
    block_definitions[ IRON_BLOCK ].textures.top = getTexture( IRON_BLOCK );
    block_definitions[ IRON_BLOCK ].textures.side = getTexture( IRON_BLOCK );
    block_definitions[ IRON_BLOCK ].textures.bottom = getTexture( IRON_BLOCK );
    block_definitions[ IRON_BLOCK ].special_grass_logic = 0;

    block_definitions[ GOLD_BLOCK ].id = GOLD_BLOCK;
    block_definitions[ GOLD_BLOCK ].alpha = 1.0f;
    block_definitions[ GOLD_BLOCK ].height = 1.0f;
    block_definitions[ GOLD_BLOCK ].textures.top = getTexture( GOLD_BLOCK );
    block_definitions[ GOLD_BLOCK ].textures.side = getTexture( GOLD_BLOCK );
    block_definitions[ GOLD_BLOCK ].textures.bottom = getTexture( GOLD_BLOCK );
    block_definitions[ GOLD_BLOCK ].special_grass_logic = 0;

    block_definitions[ DIAMOND_BLOCK ].id = DIAMOND_BLOCK;
    block_definitions[ DIAMOND_BLOCK ].alpha = 1.0f;
    block_definitions[ DIAMOND_BLOCK ].height = 1.0f;
    block_definitions[ DIAMOND_BLOCK ].textures.top = getTexture( DIAMOND_BLOCK );
    block_definitions[ DIAMOND_BLOCK ].textures.side = getTexture( DIAMOND_BLOCK );
    block_definitions[ DIAMOND_BLOCK ].textures.bottom = getTexture( DIAMOND_BLOCK );
    block_definitions[ DIAMOND_BLOCK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ SAND ].id = SAND;
    block_definitions[ SAND ].alpha = 1.0f;
    block_definitions[ SAND ].height = 1.0f;
    block_definitions[ SAND ].textures.top = getTexture( SAND );
    block_definitions[ SAND ].textures.side = getTexture( SAND );
    block_definitions[ SAND ].textures.bottom = getTexture( SAND );
    block_definitions[ SAND ].special_grass_logic = 0;

    block_definitions[ WATER ].id = WATER;
    block_definitions[ WATER ].alpha = 0.5f;
    block_definitions[ WATER ].height = 14.0f / 16.0f;
    block_definitions[ WATER ].textures.top = getTexture( WATER );
    block_definitions[ WATER ].textures.side = getTexture( WATER );
    block_definitions[ WATER ].textures.bottom = getTexture( WATER );
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