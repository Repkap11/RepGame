#ifndef HEADER_BLOCK_DEFINITIONS_H
#define HEADER_BLOCK_DEFINITIONS_H

// According to GIMP upper left pixel coord
// X/16 + 24*(Y/16) + 1
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
    ORANGE_SAND,
    SAPPLING,
    BRICK_SLAB,
    SANDSTONE_SLAB,
    COBBLESTONE_SLAB,
    BED,
    NO_BLOCK_0,
    NO_BLOCK_1,
    NO_BLOCK_2,
    NO_BLOCK_3,
    COBBLESTONE,
    BEDROCK,
    SAND,
    GRAVEL,
    WOOD_LOG_SIDE,
    WOOD_LOG_TOP,
    IRON_BLOCK,
    GOLD_BLOCK,
    DIAMOND_BLOCK,
    EMERALD_BLOCK,
    REDSTONE_BLOCK,
    WATER = 95,
    SNOW = 99,
    PLAYER_SELECTION = 1212,
    LAST_BLOCK_ID = 1200,
} BlockID;

#include "abstract/textures.hpp"

typedef struct {
    BlockID top;
    BlockID side;
    BlockID bottom;
} BlockTextureMap;

typedef enum { RenderOrderTransparent, RenderOrderTranslucent, RenderOrderSolid } RenderOrder;

typedef struct {
    BlockID id;
    BlockTextureMap textures;
    RenderOrder renderOrder;
    float height;
} Block;

void block_definitions_initilize_definitions( Texture *texture );
Block *block_definition_get_definition( BlockID blockID );
void block_definitions_free_definitions( );

#endif