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

    block_definitions[ WOOD_PLANK ].id = DIRT;
    block_definitions[ WOOD_PLANK ].alpha = 1.0f;
    block_definitions[ WOOD_PLANK ].height = 1.0f;
    block_definitions[ WOOD_PLANK ].textures.top = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.side = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].textures.bottom = getTexture( DIRT );
    block_definitions[ WOOD_PLANK ].special_grass_logic = 0;

    block_definitions[ DOUBLE_SLAB ].id = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].alpha = 1.0f;
    block_definitions[ DOUBLE_SLAB ].height = 1.0f;
    block_definitions[ DOUBLE_SLAB ].textures.top = getTexture( DOUBLE_SLAB );
    block_definitions[ DOUBLE_SLAB ].textures.side = getTexture( DOUBLE_SLAB );
    block_definitions[ DOUBLE_SLAB ].textures.bottom = getTexture( DOUBLE_SLAB );
    block_definitions[ DOUBLE_SLAB ].special_grass_logic = 0;

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