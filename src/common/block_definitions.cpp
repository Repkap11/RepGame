#include "common/block_definitions.hpp"
#include "common/abstract/textures.hpp"
#include "common/RepGame.hpp"
#include <stdlib.h>

#define SET_LEFT( block, block_face )   block_definitions[ block ].textures[ FACE_LEFT ] = block_face
#define SET_RIGHT( block, block_face )  block_definitions[ block ].textures[ FACE_RIGHT ] = block_face
#define SET_FRONT( block, block_face )  block_definitions[ block ].textures[ FACE_FRONT ] = block_face
#define SET_BACK( block, block_face )   block_definitions[ block ].textures[ FACE_BACK ] = block_face
#define SET_TOP( block, block_face )    block_definitions[ block ].textures[ FACE_TOP ] = block_face
#define SET_BOT( block, block_face )    block_definitions[ block ].textures[ FACE_BOTTOM ] = block_face

Block *block_definitions;
void do_disable( Block *block_definitions );
void do_flowers( Block *block_definitions );

void block_definitions_initilize_definitions( Texture *texture ) {
    block_definitions = ( Block * )calloc( LAST_BLOCK_ID, sizeof( Block ) );
    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];
        block->id = ( BlockID )block_id;
        block->renderOrder = RenderOrder_Opaque;
        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            block->textures[ i ] = ( BlockID )block_id;
        }
        block->no_light = NO_LIGHT_NO_DRAW;
        block->casts_shadow = true;
        block->can_mesh = true;
        block->hides_self = false;
        block->needs_place_on_solid = false;
    }
    block_definitions[ WATER ].renderOrder = RenderOrder_Water;

    SET_LEFT( GRASS, GRASS_SIDE );
    SET_RIGHT( GRASS, GRASS_SIDE );
    SET_FRONT( GRASS, GRASS_SIDE );
    SET_BACK( GRASS, GRASS_SIDE );
    SET_BOT( GRASS, DIRT );

    SET_TOP( DOUBLE_SLAB, SLAB_TOP );
    SET_LEFT( DOUBLE_SLAB, DOUBLE_SLAB );
    SET_BOT( DOUBLE_SLAB, SLAB_TOP );

    SET_TOP( TNT, TNT_TOP );
    SET_BOT( TNT, TNT_BOTTOM );

    SET_TOP( OAK_LOG, OAK_LOG_TOP );
    SET_BOT( OAK_LOG, OAK_LOG_TOP );

    SET_TOP( BIRTCH_LOG, BIRTCH_LOG_TOP );
    SET_BOT( BIRTCH_LOG, BIRTCH_LOG_TOP );

    SET_TOP( JUNGLE_LOG, JUNGLE_LOG_TOP );
    SET_BOT( JUNGLE_LOG, JUNGLE_LOG_TOP );

    SET_TOP( ACADIA_LOG, ACADIA_LOG_TOP );
    SET_BOT( ACADIA_LOG, ACADIA_LOG_TOP );

    SET_TOP( DARK_OAK_LOG, DARK_OAK_LOG_TOP );
    SET_BOT( DARK_OAK_LOG, DARK_OAK_LOG_TOP );

    SET_TOP( BOOK_CASE, OAK_PLANK );
    SET_BOT( BOOK_CASE, OAK_PLANK );

    SET_LEFT( CRAFTING_BENCH, CRAFTING_BENCH_SIDE1 );
    SET_RIGHT( CRAFTING_BENCH, CRAFTING_BENCH_SIDE1 );
    SET_FRONT( CRAFTING_BENCH, CRAFTING_BENCH_SIDE2 );
    SET_BACK( CRAFTING_BENCH, CRAFTING_BENCH_SIDE2 );
    SET_BOT( CRAFTING_BENCH, OAK_PLANK );

    SET_TOP( SINGLE_CHEST_SIDE_LATCH, SINGLE_CHEST_TOP );
    SET_FRONT( SINGLE_CHEST_SIDE_LATCH, SINGLE_CHEST_SIDE_EMPTY );
    SET_BACK( SINGLE_CHEST_SIDE_LATCH, SINGLE_CHEST_SIDE_EMPTY );
    SET_LEFT( SINGLE_CHEST_SIDE_LATCH, SINGLE_CHEST_SIDE_EMPTY );
    SET_RIGHT( SINGLE_CHEST_SIDE_LATCH, SINGLE_CHEST_SIDE_LATCH );
    SET_BOT( SINGLE_CHEST_SIDE_LATCH, SINGLE_CHEST_TOP );

    SET_TOP( SNOWY_GRASS, SNOW );
    SET_BOT( SNOWY_GRASS, DIRT );

    SET_TOP( FURNACE_UNLIT, FURNACE_TOP );
    SET_LEFT( FURNACE_UNLIT, FURNACE_UNLIT );
    SET_RIGHT( FURNACE_UNLIT, FURNACE_SIDE );
    SET_FRONT( FURNACE_UNLIT, FURNACE_SIDE );
    SET_BACK( FURNACE_UNLIT, FURNACE_SIDE );
    SET_BOT( FURNACE_UNLIT, FURNACE_TOP );

    SET_TOP( FURNACE_LIT, FURNACE_TOP );
    SET_LEFT( FURNACE_LIT, FURNACE_LIT );
    SET_RIGHT( FURNACE_LIT, FURNACE_SIDE );
    SET_FRONT( FURNACE_LIT, FURNACE_SIDE );
    SET_BACK( FURNACE_LIT, FURNACE_SIDE );
    SET_BOT( FURNACE_LIT, FURNACE_TOP );

    SET_LEFT( MYCELIUM, MYCELIUM_SIDE );
    SET_RIGHT( MYCELIUM, MYCELIUM_SIDE );
    SET_FRONT( MYCELIUM, MYCELIUM_SIDE );
    SET_BACK( MYCELIUM, MYCELIUM_SIDE );
    SET_BOT( MYCELIUM, DIRT );

    SET_LEFT( PODZEL, PODZEL_SIDE );
    SET_RIGHT( PODZEL, PODZEL_SIDE );
    SET_FRONT( PODZEL, PODZEL_SIDE );
    SET_BACK( PODZEL, PODZEL_SIDE );
    SET_BOT( PODZEL, DIRT );

    block_definitions[ WHITE_GLASS ].is_seethrough = true;
    block_definitions[ BIRTCH_LEAVES ].is_seethrough = true;
    block_definitions[ PINE_LEAF ].is_seethrough = true;
    block_definitions[ JUNGLE_LEAF ].is_seethrough = true;
    block_definitions[ LEAF ].is_seethrough = true;

    block_definitions[ WHITE_GLASS ].casts_shadow = false;

    block_definitions[ WHITE_GLASS ].can_mesh = false;
    block_definitions[ WHITE_GLASS ].can_mesh = false;
    block_definitions[ BIRTCH_LEAVES ].can_mesh = false;
    block_definitions[ PINE_LEAF ].can_mesh = false;
    block_definitions[ JUNGLE_LEAF ].can_mesh = false;
    block_definitions[ LEAF ].can_mesh = false;

    block_definitions[ BIRTCH_LEAVES ].no_light = NO_LIGHT_DRAW;
    block_definitions[ PINE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ JUNGLE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ LEAF ].no_light = NO_LIGHT_DRAW;

    block_definitions[ WHITE_GLASS ].hides_self = true;

    // Start flower section
    do_flowers( block_definitions );
    do_disable( block_definitions );

    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];
        if ( block->renderOrder == RenderOrder_Flowers ) {
            block->textures[ FACE_TOP ] = TNT;
            block->textures[ FACE_BOTTOM ] = TNT;
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_BRIGHT;
            block->casts_shadow = false;
            block->can_mesh = false;
            block->needs_place_on_solid = true;
        }
        if ( block->renderOrder == RenderOrder_Water ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_BRIGHT;
            block->casts_shadow = false;
        }
        if ( block->renderOrder == RenderOrder_Transparent ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_DRAW;
            block->casts_shadow = false;
            // block->can_mesh = true; not that it matters...
        }
    }
    block_definitions[ GRASS_TUFT ].casts_shadow = true;
    block_definitions[ GRASS_TUFT2 ].casts_shadow = true;
    block_definitions[ GRASS_TUFT3 ].casts_shadow = true;
    block_definitions[ GRASS_TUFT4 ].casts_shadow = true;
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

void do_disable( Block *block_definitions ) {
    block_definitions[ AIR ].renderOrder = RenderOrder_Transparent;
    block_definitions[ PODZEL_SIDE ].renderOrder = RenderOrder_Transparent;
    block_definitions[ SINGLE_CHEST_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ MYCELIUM_SIDE ].renderOrder = RenderOrder_Transparent;
    block_definitions[ FURNACE_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ DARK_OAK_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ ACADIA_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ JUNGLE_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ BIRTCH_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ OAK_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ TNT_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ TNT_BOTTOM ].renderOrder = RenderOrder_Transparent;
    block_definitions[ SLAB_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ GRASS_SIDE ].renderOrder = RenderOrder_Transparent;
    block_definitions[ CRAFTING_BENCH_SIDE1 ].renderOrder = RenderOrder_Transparent;
    block_definitions[ CRAFTING_BENCH_SIDE2 ].renderOrder = RenderOrder_Transparent;
}

void do_flowers( Block *block_definitions ) {
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
    block_definitions[ REED ].renderOrder = RenderOrder_Flowers;
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
}