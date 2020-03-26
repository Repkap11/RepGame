#include <stdlib.h>
#include <string.h>

#include "common/block_definitions.hpp"
#include "common/abstract/textures.hpp"
#include "common/RepGame.hpp"

Block *block_definitions;
float *block_supports_random_rotations;

void do_disable( Block *block_definitions );
void do_flowers( Block *block_definitions );

void block_definitions_get_random_rotations( float **out_supports_random_rotations ) {
    *out_supports_random_rotations = block_supports_random_rotations;
}

void block_definitions_initilize_definitions( Texture *texture ) {
    block_definitions = ( Block * )calloc( LAST_BLOCK_ID, sizeof( Block ) );
    block_supports_random_rotations = ( float * )calloc( LAST_BLOCK_ID, sizeof( float ) );
    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];
        block_supports_random_rotations[ block_id ] = 0.0f;
        block->id = ( BlockID )block_id;
        block->renderOrder = RenderOrder_Opaque;
        for ( int i = 0; i < NUM_FACES_IN_CUBE; i++ ) {
            block->textures[ i ] = ( BlockID )block_id;
        }
        block->no_light = NO_LIGHT_NO_DRAW;
        block->casts_shadow = true;
        block->can_mesh_x = true;
        block->can_mesh_y = true;
        block->can_mesh_z = true;
        block->hides_self = false; // like connected glass so you don't see the perpendicular panes, you probably need is_seethrough too.
        block->is_seethrough = false;
        block->needs_place_on_solid = false;
        block->rotate_on_placement = false;
        block->icon_is_isometric = true;
        block->is_pickable = true;
        block->collides_with_player = true;
        block->flows = 0;
        block->scale = {1, 1, 1};
        block->offset = {0, 0, 0};
        block->tex_offset = {0, 0, 0};
        block->breaks_in_liquid = 0;
        block->initial_redstone_power = 0; // redstone block

        block->affected_by_redstone_power = 0; // dust, piston
    }

    block_definitions[ REDSTONE_LAMP ].affected_by_redstone_power = 1;
    block_definitions[ REDSTONE_BLOCK ].initial_redstone_power = 10;
    block_definitions[ REDSTONE_LINE ].affected_by_redstone_power = 1;

    block_definitions[ STONE_BRICK_SLAB ].textures[ FACE_TOP ] = STONE_BRICK;
    block_definitions[ STONE_BRICK_SLAB ].textures[ FACE_BOTTOM ] = STONE_BRICK;
    block_definitions[ STONE_BRICK_SLAB ].scale = {1, 1.0f / 2.0f, 1};
    block_definitions[ STONE_BRICK_SLAB ].offset = {0, 0, 0};
    block_definitions[ STONE_BRICK_SLAB ].tex_offset = {0, -0.25, 0};
    block_definitions[ STONE_BRICK_SLAB ].is_seethrough = true;
    block_definitions[ STONE_BRICK_SLAB ].can_mesh_x = true;
    block_definitions[ STONE_BRICK_SLAB ].can_mesh_y = false;
    block_definitions[ STONE_BRICK_SLAB ].can_mesh_z = true;

    // block_definitions[ CREEPER_HEAD ].textures[ FACE_TOP ] = STONE_BRICK;
    // block_definitions[ CREEPER_HEAD ].textures[ FACE_BOTTOM ] = STONE_BRICK;
    block_definitions[ CREEPER_HEAD ].scale = {1.0f / 2.0f, 1.0f / 2.0f, 1.0f / 2.0f};
    block_definitions[ CREEPER_HEAD ].offset = {0.25, 0, 0.25};
    block_definitions[ CREEPER_HEAD ].tex_offset = {-0.25, -0.25, -0.25};
    block_definitions[ CREEPER_HEAD ].is_seethrough = true;
    block_definitions[ CREEPER_HEAD ].can_mesh_x = false;
    block_definitions[ CREEPER_HEAD ].can_mesh_y = false;
    block_definitions[ CREEPER_HEAD ].can_mesh_z = false;
    block_definitions[ CREEPER_HEAD ].casts_shadow = false;
    block_definitions[ CREEPER_HEAD ].collides_with_player = false;
    block_definitions[ CREEPER_HEAD ].casts_shadow = false;

    block_definitions[ REDSTONE_LINE ].scale = {1, 1.0f / 16.0f, 1};
    block_definitions[ REDSTONE_LINE ].offset = {0, 0, 0};
    block_definitions[ REDSTONE_LINE ].tex_offset = {0, 0, 0};
    block_definitions[ REDSTONE_LINE ].is_seethrough = true;
    block_definitions[ REDSTONE_LINE ].can_mesh_x = true;
    block_definitions[ REDSTONE_LINE ].can_mesh_y = false;
    block_definitions[ REDSTONE_LINE ].can_mesh_z = true;
    block_definitions[ REDSTONE_LINE ].casts_shadow = false;
    block_definitions[ REDSTONE_LINE ].icon_is_isometric = false;
    block_definitions[ REDSTONE_LINE ].needs_place_on_solid = true;
    block_definitions[ REDSTONE_LINE ].collides_with_player = false;

    block_definitions[ WATER ].renderOrder = RenderOrder_Water;
    block_definitions[ WATER ].flows = 20;
    block_definitions[ LAVA ].renderOrder = RenderOrder_Water;
    block_definitions[ LAVA ].flows = 20;

    block_definitions[ GRASS ].textures[ FACE_LEFT ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_RIGHT ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_FRONT ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_BACK ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_BOTTOM ] = DIRT;
    block_supports_random_rotations[ GRASS - 1 ] = 1.0f;

    block_definitions[ DOUBLE_SLAB ].textures[ FACE_TOP ] = SLAB_TOP;
    block_definitions[ DOUBLE_SLAB ].textures[ FACE_LEFT ] = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].textures[ FACE_BOTTOM ] = SLAB_TOP;

    block_definitions[ TNT ].textures[ FACE_TOP ] = TNT_TOP;
    block_definitions[ TNT ].textures[ FACE_BOTTOM ] = TNT_BOTTOM;

    block_definitions[ OAK_LOG ].textures[ FACE_TOP ] = OAK_LOG_TOP;
    block_definitions[ OAK_LOG ].textures[ FACE_BOTTOM ] = OAK_LOG_TOP;

    block_definitions[ BIRTCH_LOG ].textures[ FACE_TOP ] = BIRTCH_LOG_TOP;
    block_definitions[ BIRTCH_LOG ].textures[ FACE_BOTTOM ] = BIRTCH_LOG_TOP;

    block_definitions[ JUNGLE_LOG ].textures[ FACE_TOP ] = JUNGLE_LOG_TOP;
    block_definitions[ JUNGLE_LOG ].textures[ FACE_BOTTOM ] = JUNGLE_LOG_TOP;

    block_definitions[ ACADIA_LOG ].textures[ FACE_TOP ] = ACADIA_LOG_TOP;
    block_definitions[ ACADIA_LOG ].textures[ FACE_BOTTOM ] = ACADIA_LOG_TOP;

    block_definitions[ DARK_OAK_LOG ].textures[ FACE_TOP ] = DARK_OAK_LOG_TOP;
    block_definitions[ DARK_OAK_LOG ].textures[ FACE_BOTTOM ] = DARK_OAK_LOG_TOP;

    block_definitions[ BOOK_CASE ].textures[ FACE_TOP ] = OAK_PLANK;
    block_definitions[ BOOK_CASE ].textures[ FACE_BOTTOM ] = OAK_PLANK;

    block_definitions[ CRAFTING_BENCH ].textures[ FACE_LEFT ] = CRAFTING_BENCH_SIDE1;
    block_definitions[ CRAFTING_BENCH ].textures[ FACE_RIGHT ] = CRAFTING_BENCH_SIDE1;
    block_definitions[ CRAFTING_BENCH ].textures[ FACE_FRONT ] = CRAFTING_BENCH_SIDE2;
    block_definitions[ CRAFTING_BENCH ].textures[ FACE_BACK ] = CRAFTING_BENCH_SIDE2;
    block_definitions[ CRAFTING_BENCH ].textures[ FACE_BOTTOM ] = OAK_PLANK;

    block_definitions[ SINGLE_CHEST_LATCH ].textures[ FACE_TOP ] = SINGLE_CHEST_TOP;
    block_definitions[ SINGLE_CHEST_LATCH ].textures[ FACE_FRONT ] = SINGLE_CHEST_LATCH;
    block_definitions[ SINGLE_CHEST_LATCH ].textures[ FACE_BACK ] = CHEST_SIDE;
    block_definitions[ SINGLE_CHEST_LATCH ].textures[ FACE_LEFT ] = CHEST_SIDE;
    block_definitions[ SINGLE_CHEST_LATCH ].textures[ FACE_RIGHT ] = CHEST_SIDE;
    block_definitions[ SINGLE_CHEST_LATCH ].textures[ FACE_BOTTOM ] = SINGLE_CHEST_TOP;

    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].textures[ FACE_TOP ] = DOUBLE_CHEST_LEFT_TOP;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].textures[ FACE_FRONT ] = DOUBLE_CHEST_LEFT_LATCH;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].textures[ FACE_BACK ] = DOUBLE_CHEST_LEFT_BACK;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].textures[ FACE_LEFT ] = CHEST_SIDE;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].textures[ FACE_RIGHT ] = CHEST_SIDE;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].textures[ FACE_BOTTOM ] = DOUBLE_CHEST_LEFT_TOP;

    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_TOP ] = DOUBLE_CHEST_RIGHT_TOP;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_FRONT ] = DOUBLE_CHEST_RIGHT_LATCH;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_BACK ] = DOUBLE_CHEST_RIGHT_BACK;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_LEFT ] = CHEST_SIDE;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_RIGHT ] = CHEST_SIDE;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_BOTTOM ] = DOUBLE_CHEST_RIGHT_TOP;

    // SINGLE_CHEST_TOP = 162, CHEST_SIDE = 163, SINGLE_CHEST_LATCH = 164, DOUBLE_CHEST_LEFT_LATCH = 165, DOUBLE_CHEST_RIGHT_LATCH = 166, DOUBLE_CHEST_LEFT_BACK = 167, DOUBLE_CHEST_RIGHT_BACK = 168,

    block_definitions[ SNOWY_GRASS ].textures[ FACE_TOP ] = SNOW;
    block_definitions[ SNOWY_GRASS ].textures[ FACE_BOTTOM ] = DIRT;

    block_definitions[ FURNACE_UNLIT ].textures[ FACE_TOP ] = FURNACE_TOP;
    block_definitions[ FURNACE_UNLIT ].textures[ FACE_LEFT ] = FURNACE_SIDE;
    block_definitions[ FURNACE_UNLIT ].textures[ FACE_RIGHT ] = FURNACE_SIDE;
    block_definitions[ FURNACE_UNLIT ].textures[ FACE_FRONT ] = FURNACE_UNLIT;
    block_definitions[ FURNACE_UNLIT ].textures[ FACE_BACK ] = FURNACE_SIDE;
    block_definitions[ FURNACE_UNLIT ].textures[ FACE_BOTTOM ] = FURNACE_TOP;

    block_definitions[ FURNACE_LIT ].textures[ FACE_TOP ] = FURNACE_TOP;
    block_definitions[ FURNACE_LIT ].textures[ FACE_LEFT ] = FURNACE_SIDE;
    block_definitions[ FURNACE_LIT ].textures[ FACE_RIGHT ] = FURNACE_SIDE;
    block_definitions[ FURNACE_LIT ].textures[ FACE_FRONT ] = FURNACE_LIT;
    block_definitions[ FURNACE_LIT ].textures[ FACE_BACK ] = FURNACE_SIDE;
    block_definitions[ FURNACE_LIT ].textures[ FACE_BOTTOM ] = FURNACE_TOP;

    block_definitions[ MYCELIUM ].textures[ FACE_LEFT ] = MYCELIUM_SIDE;
    block_definitions[ MYCELIUM ].textures[ FACE_RIGHT ] = MYCELIUM_SIDE;
    block_definitions[ MYCELIUM ].textures[ FACE_FRONT ] = MYCELIUM_SIDE;
    block_definitions[ MYCELIUM ].textures[ FACE_BACK ] = MYCELIUM_SIDE;
    block_definitions[ MYCELIUM ].textures[ FACE_BOTTOM ] = DIRT;

    block_definitions[ PODZEL ].textures[ FACE_LEFT ] = PODZEL_SIDE;
    block_definitions[ PODZEL ].textures[ FACE_RIGHT ] = PODZEL_SIDE;
    block_definitions[ PODZEL ].textures[ FACE_FRONT ] = PODZEL_SIDE;
    block_definitions[ PODZEL ].textures[ FACE_BACK ] = PODZEL_SIDE;
    block_definitions[ PODZEL ].textures[ FACE_BOTTOM ] = DIRT;

    block_definitions[ WHITE_GLASS ].is_seethrough = true;
    block_definitions[ BIRTCH_LEAVES ].is_seethrough = true;
    block_definitions[ PINE_LEAF ].is_seethrough = true;
    block_definitions[ JUNGLE_LEAF ].is_seethrough = true;
    block_definitions[ LEAF ].is_seethrough = true;

    block_definitions[ WHITE_GLASS ].casts_shadow = false;

    block_definitions[ WHITE_GLASS ].can_mesh_x = false;
    block_definitions[ WHITE_GLASS ].can_mesh_y = false;
    block_definitions[ WHITE_GLASS ].can_mesh_z = false;
    block_definitions[ BIRTCH_LEAVES ].can_mesh_x = false;
    block_definitions[ BIRTCH_LEAVES ].can_mesh_y = false;
    block_definitions[ BIRTCH_LEAVES ].can_mesh_z = false;
    block_definitions[ PINE_LEAF ].can_mesh_x = false;
    block_definitions[ PINE_LEAF ].can_mesh_y = false;
    block_definitions[ PINE_LEAF ].can_mesh_z = false;
    block_definitions[ JUNGLE_LEAF ].can_mesh_x = false;
    block_definitions[ JUNGLE_LEAF ].can_mesh_y = false;
    block_definitions[ JUNGLE_LEAF ].can_mesh_z = false;
    block_definitions[ LEAF ].can_mesh_x = false;
    block_definitions[ LEAF ].can_mesh_y = false;
    block_definitions[ LEAF ].can_mesh_z = false;

    block_definitions[ WHITE_GLASS ].no_light = NO_LIGHT_NO_DRAW;
    block_definitions[ BIRTCH_LEAVES ].no_light = NO_LIGHT_DRAW;
    block_definitions[ PINE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ JUNGLE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ LEAF ].no_light = NO_LIGHT_DRAW;

    block_definitions[ WHITE_GLASS ].hides_self = true;

    block_definitions[ FURNACE_LIT ].rotate_on_placement = true;
    block_definitions[ FURNACE_UNLIT ].rotate_on_placement = true;
    block_definitions[ SINGLE_CHEST_LATCH ].rotate_on_placement = true;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].rotate_on_placement = true;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].rotate_on_placement = true;

    // Start flower section
    do_flowers( block_definitions );
    do_disable( block_definitions );

    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];

        // Calculate non_full_size so the result can be cached for access at runtime.
        if ( ( block->scale.x != 1 || block->scale.y != 1 || block->scale.x != 1 ) ||    //
             ( block->offset.x != 0 || block->offset.y != 0 || block->offset.x != 0 ) || //
             ( block->tex_offset.x != 0 || block->tex_offset.y != 0 || block->tex_offset.x != 0 ) ) {
            block->non_full_size = true;
        } else {
            block->non_full_size = false;
        }

        if ( block->renderOrder == RenderOrder_Flowers ) {
            block->textures[ FACE_TOP ] = TNT;
            block->textures[ FACE_BOTTOM ] = TNT;
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_BRIGHT;
            block->casts_shadow = false;
            block->can_mesh_x = false;
            block->can_mesh_y = true;
            block->can_mesh_z = false;
            block->needs_place_on_solid = true;
            block->icon_is_isometric = false;
            block->is_pickable = true;
            block->collides_with_player = false;
            block->breaks_in_liquid = true;
        }
        if ( block->renderOrder == RenderOrder_Water ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_BRIGHT;
            block->casts_shadow = false;
            block->icon_is_isometric = false;
            block->hides_self = true;
            block->is_pickable = false;
            block->collides_with_player = false;
        }
        if ( block->renderOrder == RenderOrder_Transparent ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_DRAW;
            block->casts_shadow = false;
            block->is_pickable = false;
            block->collides_with_player = false;
            block->breaks_in_liquid = true;
            // block->can_mesh = true; not that it matters...
        }
    }

    // Exceptions
    block_definitions[ SPIDER_WEB ].needs_place_on_solid = false;
    block_definitions[ BARRIER ].needs_place_on_solid = false;
    block_definitions[ DARK_BARRIER ].needs_place_on_solid = false;

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
    free( block_supports_random_rotations );
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
    block_definitions[ REDSTONE_LINE_POWERED ].renderOrder = RenderOrder_Transparent;
    block_definitions[ REDSTONE_LAMP_POWERED ].renderOrder = RenderOrder_Transparent;
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

bool BlockStates_equal( const BlockState &a, const BlockState &b ) {
    return !memcmp( &a, &b, sizeof( BlockState ) );
}
