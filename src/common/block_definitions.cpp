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
        block->textures.top = ( BlockID )block_id;
        block->textures.side = ( BlockID )block_id;
        block->textures.bottom = ( BlockID )block_id;
        block->no_light = NO_LIGHT_NO_DRAW;
        block->casts_shadow = 1;
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

    block_definitions[ OAK_LOG ].textures.top = OAK_LOG_TOP;
    block_definitions[ OAK_LOG ].textures.bottom = OAK_LOG_TOP;
    block_definitions[ OAK_LOG_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ BIRTCH_LOG ].textures.top = BIRTCH_LOG_TOP;
    block_definitions[ BIRTCH_LOG ].textures.bottom = BIRTCH_LOG_TOP;
    block_definitions[ BIRTCH_LOG_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ JUNGLE_LOG ].textures.top = JUNGLE_LOG_TOP;
    block_definitions[ JUNGLE_LOG ].textures.bottom = JUNGLE_LOG_TOP;
    block_definitions[ JUNGLE_LOG_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ ACADIA_LOG ].textures.top = ACADIA_LOG_TOP;
    block_definitions[ ACADIA_LOG ].textures.bottom = ACADIA_LOG_TOP;
    block_definitions[ ACADIA_LOG_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ DARK_OAK_LOG ].textures.top = DARK_OAK_LOG_TOP;
    block_definitions[ DARK_OAK_LOG ].textures.bottom = DARK_OAK_LOG_TOP;
    block_definitions[ DARK_OAK_LOG_TOP ].renderOrder = RenderOrder_Transparent;

    block_definitions[ WATER ].renderOrder = RenderOrder_Water;
    block_definitions[ WHITE_GLASS ].renderOrder = RenderOrder_GlassLeafs;

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

    block_definitions[ RED_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ YELLOW_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ SPIDER_WEB ].renderOrder = RenderOrder_Flowers;
    block_definitions[ RED_MUSHROOM ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BROWN_MUSHROOM ].renderOrder = RenderOrder_Flowers;
    block_definitions[ JUNGLE_SAPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BARRIER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ DARK_BARRIER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ DARK_DEAD_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRASS_TUFT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ LIGHT_DEAD_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GARBAGE_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ DARK_OAK_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLUE_CORAL ].renderOrder = RenderOrder_Flowers;
    block_definitions[ FIRE ].renderOrder = RenderOrder_Flowers;
    block_definitions[ RED_MUSHROOM_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ EMPTY_SPAWNER ].renderOrder = RenderOrder_GlassLeafs;
    block_definitions[ BIRTCH_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLUE_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_0 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_3 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_4 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_5 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_6 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHEAT_7 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ MELLON_STEM ].renderOrder = RenderOrder_Flowers;
    block_definitions[ RED_FLOWER_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ DRAGON_EGG ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PURPLE_CORAL ].renderOrder = RenderOrder_Flowers;
    block_definitions[ RED_CORAL ].renderOrder = RenderOrder_Flowers;
    block_definitions[ EMPTY_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CARROT_0 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CARROT_1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CARROT_2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CARROT_3 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CARROT_4 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ NETHER_WART_1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ NETHER_WART_2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ NETHER_WART_3 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ YELLOW_CORAL ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CHORUS_0 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CHORUS_1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CHORUS_2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ FAT_OAK_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ ACACIA_SAPPLING ].renderOrder = RenderOrder_Flowers;
    block_definitions[ POPPY_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ LARGE_BLUE_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ LARGE_PURPLE_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ LARGE_WHITE_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ RED_TULIP ].renderOrder = RenderOrder_Flowers;
    block_definitions[ ORANGE_TULIP ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHITE_TULIP ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PINK_TULIP ].renderOrder = RenderOrder_Flowers;
    block_definitions[ SMALL_SUNFLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ LARGE_SUNFLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ SMALL_LILLIC_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRASS_TUFT2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRASS_TUFT3 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PODZEL ].textures.side = PODZEL_SIDE;
    block_definitions[ PODZEL ].textures.bottom = DIRT;
    block_definitions[ PODZEL_SIDE ].renderOrder = RenderOrder_Transparent;
    block_definitions[ GRASS_TUFT4 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BEAT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLUE_CORAL2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CACTUS_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PURPLE_CORAL2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ UNDERWATER_TUFT_SHORT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ UNDERWATER_TUFT_TALL ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PURPLE_CORAL3 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ RED_CORAL2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ YELLOW_CORAL2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ SEAWEED ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CORAL_SPOTS1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ CORAL_SPOTS2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_3 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_4 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_5 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_6 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_7 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_8 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_9 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ GRAYCALE_CORAL_10 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PINECONE ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLUE_FLOWER2 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHITE_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLACK_FLOWER ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BAMBOO ].renderOrder = RenderOrder_Flowers;
    block_definitions[ FLOWER_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PURPLE_FLOWER_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHITE_FLOWER_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BAMBOO_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BIRTCH_SAPLING_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLUE_FLOWER_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BROWN_MUSHROOM_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ BLUE_FLOWER2_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ YELLOW_FLOWER_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ OAK_SAPPLING_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ LIGHT_DEAD_SAPPLING_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ PINE_SAPPLING_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ DARK_OAK_SAPPLING_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ WHITE_FLOWER_SAPPLING_IN_POT ].renderOrder = RenderOrder_Flowers;
    block_definitions[ END_ROD ].renderOrder = RenderOrder_Flowers;

    block_definitions[ LEAF ].renderOrder = RenderOrder_GlassLeafs;
    block_definitions[ BIRTCH_LEAVES ].renderOrder = RenderOrder_GlassLeafs;
    block_definitions[ PINE_LEAF ].renderOrder = RenderOrder_GlassLeafs;
    block_definitions[ JUNGLE_LEAF ].renderOrder = RenderOrder_GlassLeafs;

    block_definitions[ LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ BIRTCH_LEAVES ].no_light = NO_LIGHT_DRAW;
    block_definitions[ PINE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ JUNGLE_LEAF ].no_light = NO_LIGHT_DRAW;

    block_definitions[ WATER ].no_light = NO_LIGHT_BRIGHT;
    block_definitions[ WATER ].casts_shadow = 0;
    block_definitions[ WHITE_GLASS ].casts_shadow = 0;

    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];
        if ( block->renderOrder == RenderOrder_Flowers ) {
            block->textures.top = TNT;
            block->textures.bottom = TNT;
            block->no_light = NO_LIGHT_BRIGHT;
            block->casts_shadow = false;
            block->is_seethrough = true;
        }
        if ( block->renderOrder == RenderOrder_GlassLeafs ||  //
             block->renderOrder == RenderOrder_Transparent || //
             block->renderOrder == RenderOrder_Water ) {
            block->is_seethrough = true;
        }
        if ( block->renderOrder == RenderOrder_Transparent ) {
            block->no_light = NO_LIGHT_DRAW;
            block->casts_shadow = false;
        }
        if ( block->no_light == NO_LIGHT_DRAW ) {
            block->is_seethrough = true;
        }
    }
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