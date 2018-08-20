#include "block_definitions.hpp"
#include "abstract/textures.hpp"
#include "RepGame.hpp"
#include <stdlib.h>

Block *block_definitions;

void block_definitions_initilize_definitions( Texture *texture ) {
    block_definitions = ( Block * )calloc( LAST_BLOCK_ID, sizeof( Block ) );
    block_definitions[ AIR ].id = AIR;
    block_definitions[ AIR ].alpha = 0.0f;
    block_definitions[ AIR ].height = 0.0f;
    block_definitions[ AIR ].textures.top = AIR;
    block_definitions[ AIR ].textures.side = AIR;
    block_definitions[ AIR ].textures.bottom = AIR;

    block_definitions[ PLAYER_SELECTION ].id = PLAYER_SELECTION;
    block_definitions[ PLAYER_SELECTION ].alpha = 0.2f;
    block_definitions[ PLAYER_SELECTION ].height = 1.0f;
    block_definitions[ PLAYER_SELECTION ].textures.top = PLAYER_SELECTION;
    block_definitions[ PLAYER_SELECTION ].textures.side = PLAYER_SELECTION;
    block_definitions[ PLAYER_SELECTION ].textures.bottom = PLAYER_SELECTION;
    block_definitions[ PLAYER_SELECTION ].special_grass_logic = 0;

    block_definitions[ GRASS ].id = GRASS;
    block_definitions[ GRASS ].alpha = 1.0f;
    block_definitions[ GRASS ].height = 1.0f;
    block_definitions[ GRASS ].textures.top = GRASS;
    block_definitions[ GRASS ].textures.side = GRASS_SIDE;
    block_definitions[ GRASS ].textures.bottom = DIRT;
    block_definitions[ GRASS ].special_grass_logic = 1;

    block_definitions[ STONE ].id = STONE;
    block_definitions[ STONE ].alpha = 1.0f;
    block_definitions[ STONE ].height = 1.0f;
    block_definitions[ STONE ].textures.top = STONE;
    block_definitions[ STONE ].textures.side = STONE;
    block_definitions[ STONE ].textures.bottom = STONE;
    block_definitions[ STONE ].special_grass_logic = 0;

    block_definitions[ DIRT ].id = DIRT;
    block_definitions[ DIRT ].alpha = 1.0f;
    block_definitions[ DIRT ].height = 1.0f;
    block_definitions[ DIRT ].textures.top = DIRT;
    block_definitions[ DIRT ].textures.side = DIRT;
    block_definitions[ DIRT ].textures.bottom = DIRT;
    block_definitions[ DIRT ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].textures.side = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].textures.bottom = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ DOUBLE_SLAB ].id = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].alpha = 1.0f;
    block_definitions[ DOUBLE_SLAB ].height = 1.0f;
    block_definitions[ DOUBLE_SLAB ].textures.top = SLAB_TOP;
    block_definitions[ DOUBLE_SLAB ].textures.side = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].textures.bottom = SLAB_TOP;
    block_definitions[ DOUBLE_SLAB ].special_grass_logic = 0;

    block_definitions[ BRICK ].id = BRICK;
    block_definitions[ BRICK ].alpha = 1.0f;
    block_definitions[ BRICK ].height = 1.0f;
    block_definitions[ BRICK ].textures.top = BRICK;
    block_definitions[ BRICK ].textures.side = BRICK;
    block_definitions[ BRICK ].textures.bottom = BRICK;
    block_definitions[ BRICK ].special_grass_logic = 0;

    block_definitions[ TNT ].id = TNT;
    block_definitions[ TNT ].alpha = 1.0f;
    block_definitions[ TNT ].height = 1.0f;
    block_definitions[ TNT ].textures.top = TNT_TOP;
    block_definitions[ TNT ].textures.side = TNT;
    block_definitions[ TNT ].textures.bottom = TNT_BOTTOM;
    block_definitions[ TNT ].special_grass_logic = 0;

    block_definitions[ COBBLESTONE ].id = COBBLESTONE;
    block_definitions[ COBBLESTONE ].alpha = 1.0f;
    block_definitions[ COBBLESTONE ].height = 1.0f;
    block_definitions[ COBBLESTONE ].textures.top = COBBLESTONE;
    block_definitions[ COBBLESTONE ].textures.side = COBBLESTONE;
    block_definitions[ COBBLESTONE ].textures.bottom = COBBLESTONE;
    block_definitions[ COBBLESTONE ].special_grass_logic = 0;

    block_definitions[ BEDROCK ].id = BEDROCK;
    block_definitions[ BEDROCK ].alpha = 1.0f;
    block_definitions[ BEDROCK ].height = 1.0f;
    block_definitions[ BEDROCK ].textures.top = BEDROCK;
    block_definitions[ BEDROCK ].textures.side = BEDROCK;
    block_definitions[ BEDROCK ].textures.bottom = BEDROCK;
    block_definitions[ BEDROCK ].special_grass_logic = 0;

    block_definitions[ GRAVEL ].id = GRAVEL;
    block_definitions[ GRAVEL ].alpha = 1.0f;
    block_definitions[ GRAVEL ].height = 1.0f;
    block_definitions[ GRAVEL ].textures.top = GRAVEL;
    block_definitions[ GRAVEL ].textures.side = GRAVEL;
    block_definitions[ GRAVEL ].textures.bottom = GRAVEL;
    block_definitions[ GRAVEL ].special_grass_logic = 0;

    block_definitions[ WOOD_LOG_TOP ].id = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].alpha = 1.0f;
    block_definitions[ WOOD_LOG_TOP ].height = 1.0f;
    block_definitions[ WOOD_LOG_TOP ].textures.top = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].textures.side = WOOD_LOG_SIDE;
    block_definitions[ WOOD_LOG_TOP ].textures.bottom = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].special_grass_logic = 0;

    block_definitions[ IRON_BLOCK ].id = IRON_BLOCK;
    block_definitions[ IRON_BLOCK ].alpha = 1.0f;
    block_definitions[ IRON_BLOCK ].height = 1.0f;
    block_definitions[ IRON_BLOCK ].textures.top = IRON_BLOCK;
    block_definitions[ IRON_BLOCK ].textures.side = IRON_BLOCK;
    block_definitions[ IRON_BLOCK ].textures.bottom = IRON_BLOCK;
    block_definitions[ IRON_BLOCK ].special_grass_logic = 0;

    block_definitions[ GOLD_BLOCK ].id = GOLD_BLOCK;
    block_definitions[ GOLD_BLOCK ].alpha = 1.0f;
    block_definitions[ GOLD_BLOCK ].height = 1.0f;
    block_definitions[ GOLD_BLOCK ].textures.top = GOLD_BLOCK;
    block_definitions[ GOLD_BLOCK ].textures.side = GOLD_BLOCK;
    block_definitions[ GOLD_BLOCK ].textures.bottom = GOLD_BLOCK;
    block_definitions[ GOLD_BLOCK ].special_grass_logic = 0;

    block_definitions[ DIAMOND_BLOCK ].id = DIAMOND_BLOCK;
    block_definitions[ DIAMOND_BLOCK ].alpha = 1.0f;
    block_definitions[ DIAMOND_BLOCK ].height = 1.0f;
    block_definitions[ DIAMOND_BLOCK ].textures.top = DIAMOND_BLOCK;
    block_definitions[ DIAMOND_BLOCK ].textures.side = DIAMOND_BLOCK;
    block_definitions[ DIAMOND_BLOCK ].textures.bottom = DIAMOND_BLOCK;
    block_definitions[ DIAMOND_BLOCK ].special_grass_logic = 0;

    block_definitions[ WOOD_PLANK ].id = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].textures.side = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].textures.bottom = WOOD_PLANK;
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ WOOD_LOG_TOP ].id = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].alpha = 1.0f;
    block_definitions[ WOOD_LOG_TOP ].height = 1.0f;
    block_definitions[ WOOD_LOG_TOP ].textures.top = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].textures.side = WOOD_LOG_SIDE;
    block_definitions[ WOOD_LOG_TOP ].textures.bottom = WOOD_LOG_TOP;
    block_definitions[ WOOD_LOG_TOP ].special_grass_logic = 0;

    block_definitions[ SAND ].id = SAND;
    block_definitions[ SAND ].alpha = 1.0f;
    block_definitions[ SAND ].height = 1.0f;
    block_definitions[ SAND ].textures.top = SAND;
    block_definitions[ SAND ].textures.side = SAND;
    block_definitions[ SAND ].textures.bottom = SAND;
    block_definitions[ SAND ].special_grass_logic = 0;

    block_definitions[ SNOW ].id = SNOW;
    block_definitions[ SNOW ].alpha = 1.0f;
    block_definitions[ SNOW ].height = 1.0f;
    block_definitions[ SNOW ].textures.top = SNOW;
    block_definitions[ SNOW ].textures.side = SNOW;
    block_definitions[ SNOW ].textures.bottom = SNOW;
    block_definitions[ SNOW ].special_grass_logic = 0;

    block_definitions[ WATER ].id = WATER;
    block_definitions[ WATER ].alpha = 0.9f;
    block_definitions[ WATER ].height = 1.0f;
    block_definitions[ WATER ].textures.top = WATER;
    block_definitions[ WATER ].textures.side = WATER;
    block_definitions[ WATER ].textures.bottom = WATER;
    block_definitions[ WATER ].special_grass_logic = 0;
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