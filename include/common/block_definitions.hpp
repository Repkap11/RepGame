#ifndef HEADER_BLOCK_DEFINITIONS_H
#define HEADER_BLOCK_DEFINITIONS_H

typedef enum {
    AIR = 0,
    GRASS,
    STONE,
    DIRT,
    GRASS_SIDE,
    WOOD_PLANK,
    DOUBLE_SLAB,
    SLAB_TOP,
    BRICK,
    TNT,
    TNT_TOP,
    TNT_BOTTOM,
    SPIDER_WEB,
    RED_FLOWER,
    YELLOW_FLOWER,
    NO_BLOCK,
    SAPPLING,
    COBBLESTONE,
    BEDROCK,
    SAND,
    GRAVEL,
    WOOD_LOG_SIDE,
    WOOD_LOG_TOP,
    IRON_BLOCK,
    GOLD_BLOCK,
    DIAMOND_BLOCK,
    SNOW = 67,
    PLAYER_SELECTION = 139,
    WATER = 208,
    LAST_BLOCK_ID = 255,
} BlockID;

#include "abstract/textures.hpp"

typedef struct {
    BlockID top;
    BlockID side;
    BlockID bottom;
} BlockTextureMap;

typedef struct {
    BlockID id;
    BlockTextureMap textures;
    float alpha;
    float height;
    int special_grass_logic;
} Block;

void block_definitions_initilize_definitions( Texture *texture );
Block *block_definition_get_definition( BlockID blockID );
void block_definitions_free_definitions( );

#endif