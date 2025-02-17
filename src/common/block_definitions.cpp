#include <stdlib.h>
#include <string.h>

#include "common/block_definitions.hpp"
#include "common/renderer/texture.hpp"
#include "common/RepGame.hpp"

Block *block_definitions;
float block_supports_random_rotations[ MAX_ROTATABLE_BLOCK + 1 ];

void do_disable( Block *block_definitions );
void do_flowers( Block *block_definitions );

float *block_definitions_get_random_rotations( ) {
    // Offset by 1 since the shader uses block indexes which are offset from what we use here.
    return &block_supports_random_rotations[ 1 ];
}

inline void centered_border( BlockID id, short border_size, short height ) {
    block_definitions[ id ].scale = { ( short )( 16 - 2 * border_size ), height, ( short )( 16 - 2 * border_size ) };
    block_definitions[ id ].offset = { border_size, 0, border_size };
    block_definitions[ id ].tex_offset = { ( short )( -border_size ), 0, ( short )( -border_size ) };
}

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
        block->hides_self = { false, false, false }; // like connected glass so you don't see the perpendicular panes, you probably need is_seethrough too.
        block->is_seethrough = false;
        for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
            block->needs_place_on_any_solid[ face ] = false;
        }
        block->rotate_on_placement = false;
        block->icon_is_isometric = true;
        block->is_pickable = true;
        block->collides_with_player = true;
        block->flows = 0;
        block->scale = { 16, 16, 16 };
        block->offset = { 0, 0, 0 };
        block->tex_offset = { 0, 0, 0 };
        block->breaks_in_liquid = false;
        block->initial_redstone_power = 0;       // redstone block
        block->affected_by_redstone_power = 1;   // dust, piston, all solid blocks
        block->transmits_redstone_power = false; // dust
        block->needs_place_on_solid_but_can_stack_on_self = false;
        block->can_be_placed_in = false;
        block->inventory_non_isometric_id = ( BlockID )block_id;
    }

    BlockID pressure_plate_shaped[] = { PINE_PRESSURE_PLATE, BIRTCH_PRESSURE_PLATE, JUNGLE_PRESSURE_PLATE, ACACIA_PRESSURE_PLATE, DARK_OAK_PRESSURE_PLATE };
    for ( unsigned int i = 0; i < sizeof( pressure_plate_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = pressure_plate_shaped[ i ];
        centered_border( id, 1, 1 );
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_TOP ] = true;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].breaks_in_liquid = false;
        block_definitions[ id ].rotate_on_placement = false;
    }
    // block_definitions[ OAK_PRESSURE_PLATE ].textures[ FACE_FRONT ] = OAK_PLANK;
    // block_definitions[ OAK_PRESSURE_PLATE ].textures[ FACE_BACK ] = OAK_PLANK;
    // block_definitions[ OAK_PRESSURE_PLATE ].textures[ FACE_RIGHT ] = OAK_PLANK;
    // block_definitions[ OAK_PRESSURE_PLATE ].textures[ FACE_LEFT ] = OAK_PLANK;

    block_definitions[ PINE_PRESSURE_PLATE ].textures[ FACE_FRONT ] = PINE_PLANK;
    block_definitions[ PINE_PRESSURE_PLATE ].textures[ FACE_BACK ] = PINE_PLANK;
    block_definitions[ PINE_PRESSURE_PLATE ].textures[ FACE_RIGHT ] = PINE_PLANK;
    block_definitions[ PINE_PRESSURE_PLATE ].textures[ FACE_LEFT ] = PINE_PLANK;

    block_definitions[ BIRTCH_PRESSURE_PLATE ].textures[ FACE_FRONT ] = BIRTCH_PLANK;
    block_definitions[ BIRTCH_PRESSURE_PLATE ].textures[ FACE_BACK ] = BIRTCH_PLANK;
    block_definitions[ BIRTCH_PRESSURE_PLATE ].textures[ FACE_RIGHT ] = BIRTCH_PLANK;
    block_definitions[ BIRTCH_PRESSURE_PLATE ].textures[ FACE_LEFT ] = BIRTCH_PLANK;

    block_definitions[ JUNGLE_PRESSURE_PLATE ].textures[ FACE_FRONT ] = JUNGLE_PLANK;
    block_definitions[ JUNGLE_PRESSURE_PLATE ].textures[ FACE_BACK ] = JUNGLE_PLANK;
    block_definitions[ JUNGLE_PRESSURE_PLATE ].textures[ FACE_RIGHT ] = JUNGLE_PLANK;
    block_definitions[ JUNGLE_PRESSURE_PLATE ].textures[ FACE_LEFT ] = JUNGLE_PLANK;

    block_definitions[ ACACIA_PRESSURE_PLATE ].textures[ FACE_FRONT ] = ACACIA_PLANK;
    block_definitions[ ACACIA_PRESSURE_PLATE ].textures[ FACE_BACK ] = ACACIA_PLANK;
    block_definitions[ ACACIA_PRESSURE_PLATE ].textures[ FACE_RIGHT ] = ACACIA_PLANK;
    block_definitions[ ACACIA_PRESSURE_PLATE ].textures[ FACE_LEFT ] = ACACIA_PLANK;

    block_definitions[ DARK_OAK_PRESSURE_PLATE ].textures[ FACE_FRONT ] = DARK_OAK_PLANK;
    block_definitions[ DARK_OAK_PRESSURE_PLATE ].textures[ FACE_BACK ] = DARK_OAK_PLANK;
    block_definitions[ DARK_OAK_PRESSURE_PLATE ].textures[ FACE_RIGHT ] = DARK_OAK_PLANK;
    block_definitions[ DARK_OAK_PRESSURE_PLATE ].textures[ FACE_LEFT ] = DARK_OAK_PLANK;

    BlockID button_shaped[] = { OAK_BUTTON, PINE_BUTTON, BIRTCH_BUTTON, JUNGLE_BUTTON, ACACIA_BUTTON, DARK_OAK_BUTTON, STONE_BUTTON };
    for ( unsigned int i = 0; i < sizeof( button_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = button_shaped[ i ];
        block_definitions[ id ].scale = { 6, 4, 2 };
        block_definitions[ id ].offset = { 5, 6, 0 };
        block_definitions[ id ].tex_offset = { -7, -6, -5 };
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_FRONT ] = true;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].breaks_in_liquid = true;
        block_definitions[ id ].rotate_on_placement = true;
    }
    block_definitions[ OAK_BUTTON ].textures[ FACE_TOP ] = OAK_PLANK;
    block_definitions[ OAK_BUTTON ].textures[ FACE_BOTTOM ] = OAK_PLANK;

    block_definitions[ PINE_BUTTON ].textures[ FACE_TOP ] = PINE_PLANK;
    block_definitions[ PINE_BUTTON ].textures[ FACE_BOTTOM ] = PINE_PLANK;

    block_definitions[ BIRTCH_BUTTON ].textures[ FACE_TOP ] = BIRTCH_PLANK;
    block_definitions[ BIRTCH_BUTTON ].textures[ FACE_BOTTOM ] = BIRTCH_PLANK;

    block_definitions[ JUNGLE_BUTTON ].textures[ FACE_TOP ] = JUNGLE_PLANK;
    block_definitions[ JUNGLE_BUTTON ].textures[ FACE_BOTTOM ] = JUNGLE_PLANK;

    block_definitions[ ACACIA_BUTTON ].textures[ FACE_TOP ] = ACACIA_PLANK;
    block_definitions[ ACACIA_BUTTON ].textures[ FACE_BOTTOM ] = ACACIA_PLANK;

    block_definitions[ DARK_OAK_BUTTON ].textures[ FACE_TOP ] = DARK_OAK_PLANK;
    block_definitions[ DARK_OAK_BUTTON ].textures[ FACE_BOTTOM ] = DARK_OAK_PLANK;

    block_definitions[ STONE_BUTTON ].textures[ FACE_TOP ] = STONE;
    block_definitions[ STONE_BUTTON ].textures[ FACE_BOTTOM ] = STONE;

    BlockID cake_shaped[] = { CAKE };
    for ( unsigned int i = 0; i < sizeof( cake_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = cake_shaped[ i ];
        block_definitions[ id ].scale = { 14, 8, 14 };
        block_definitions[ id ].offset = { 1, 0, 1 };
        block_definitions[ id ].tex_offset = { -1, 0, -1 };
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = true;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_TOP ] = true;
        block_definitions[ id ].collides_with_player = true;
        block_definitions[ id ].breaks_in_liquid = false;
        block_definitions[ id ].rotate_on_placement = true;
    }
    block_definitions[ CAKE ].textures[ FACE_FRONT ] = CAKE_SIDE;
    block_definitions[ CAKE ].textures[ FACE_BACK ] = CAKE_SIDE;
    block_definitions[ CAKE ].textures[ FACE_RIGHT ] = CAKE_SIDE;
    block_definitions[ CAKE ].textures[ FACE_LEFT ] = CAKE_SIDE;
    block_definitions[ CAKE ].textures[ FACE_BOTTOM ] = CAKE_BOTTOM;

    BlockID ladder_shaped[] = { LADDER };
    for ( unsigned int i = 0; i < sizeof( ladder_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = ladder_shaped[ i ];
        block_definitions[ id ].scale = { 14, 16, 1 };
        block_definitions[ id ].offset = { 1, 0, 0 };
        block_definitions[ id ].tex_offset = { -5, 0, -1 };
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_FRONT ] = true;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].breaks_in_liquid = false;
        block_definitions[ id ].rotate_on_placement = true;
        block_definitions[ id ].textures[ FACE_TOP ] = AIR;
        block_definitions[ id ].textures[ FACE_BOTTOM ] = AIR;
        block_definitions[ id ].hides_self = { false, false, false };
    }
    block_definitions[ LADDER ].textures[ FACE_TOP ] = LADDER_TOP;
    block_definitions[ LADDER ].textures[ FACE_BOTTOM ] = LADDER_TOP;

    BlockID door_shaped[] = { ( BlockID )146, ( BlockID )523, ( BlockID )147, ( BlockID )524, ( BlockID )525, ( BlockID )526, ( BlockID )527,
                              ( BlockID )122, ( BlockID )499, ( BlockID )123, ( BlockID )500, ( BlockID )501, ( BlockID )502, ( BlockID )503 };
    for ( unsigned int i = 0; i < sizeof( door_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = door_shaped[ i ];
        block_definitions[ id ].scale = { 16, 16, 1 };
        block_definitions[ id ].offset = { 0, 0, 0 };
        block_definitions[ id ].tex_offset = { 0, 0, 0 };
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_TOP ] = true;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].breaks_in_liquid = false;
        block_definitions[ id ].rotate_on_placement = true;
        block_definitions[ id ].textures[ FACE_TOP ] = AIR;
        block_definitions[ id ].textures[ FACE_BOTTOM ] = AIR;
    }

    block_definitions[ REED ].needs_place_on_solid_but_can_stack_on_self = true;

    block_definitions[ REDSTONE_LAMP ].affected_by_redstone_power = true;

    block_definitions[ REDSTONE_BLOCK ].initial_redstone_power = 10;
    block_definitions[ REDSTONE_BLOCK ].affected_by_redstone_power = false;

    block_definitions[ REDSTONE_TORCH ].affected_by_redstone_power = true;
    block_definitions[ REDSTONE_TORCH ].transmits_redstone_power = true;

    BlockID pane_shaped[] = { GLASS_PANE };
    for ( unsigned int i = 0; i < sizeof( pane_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = pane_shaped[ i ];
        block_definitions[ id ].scale = { 16, 16, 2 };
        block_definitions[ id ].offset = { 0, 0, 7 };
        block_definitions[ id ].tex_offset = { -7, 0, 0 };
        block_definitions[ id ].rotate_on_placement = true;
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        // block_definitions[ id ].hides_self = true;
    }

    BlockID torch_shaped[] = { TORCH, BURNT_OUT_TORCH, REDSTONE_TORCH, REDSTONE_TORCH_OFF, BLACK_TORCH, BLUE_TORCH, WHITE_TORCH, PURPLE_TORCH, RED_TORCH, CYAN_TORCH };
    for ( unsigned int i = 0; i < sizeof( torch_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = torch_shaped[ i ];
        centered_border( id, 7, 10 );
        // block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_TOP ] = true;
        block_definitions[ id ].breaks_in_liquid = true;
    }
    BlockID flower_shaped[] = { RED_MUSHROOM, BROWN_MUSHROOM, BLUE_FLOWER, TURTLE_EGGS1, TURTLE_EGGS2, EMPTY_POT, POPPY_FLOWER, LARGE_WHITE_FLOWER, BLACK_FLOWER };
    for ( unsigned int i = 0; i < sizeof( flower_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = flower_shaped[ i ];
        centered_border( id, 4, 11 );
    }
    BlockID red_flower_shaped[] = { RED_FLOWER, BLUE_FLOWER };
    for ( unsigned int i = 0; i < sizeof( red_flower_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = red_flower_shaped[ i ];
        centered_border( id, 5, 11 );
    }
    BlockID yellow_flower_shaped[] = { YELLOW_FLOWER };
    for ( unsigned int i = 0; i < sizeof( yellow_flower_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = yellow_flower_shaped[ i ];
        centered_border( id, 5, 9 );
    }
    BlockID potted_flower_shaped[] = { RED_FLOWER_IN_POT };
    for ( unsigned int i = 0; i < sizeof( potted_flower_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = potted_flower_shaped[ i ];
        centered_border( id, 4, 16 );
    }

    BlockID grass_tuft_shaped[] = { GRASS_TUFT2 };
    for ( unsigned int i = 0; i < sizeof( grass_tuft_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = grass_tuft_shaped[ i ];
        centered_border( id, 1, 16 );
    }

    BlockID slab_shaped[] = { STONE_BRICK_SLAB, DARK_OAK_SLAB, OAK_SLAB, BIRTCH_SLAB, JUNGLE_SLAB, ACACIA_SLAB, RED_SANDSTONE_BRICK_SLAB, PINE_SLAB, COBBLESTONE_SLAB, SANDSTONE_SLAB };
    for ( unsigned int i = 0; i < sizeof( slab_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = slab_shaped[ i ];
        block_definitions[ id ].scale = { 16, 8, 16 };
        block_definitions[ id ].offset = { 0, 0, 0 };
        block_definitions[ id ].tex_offset = { 0, -4, 0 };
        // block_definitions[ id ].is_seethrough = true;
    }

    BlockID portal_shaped[] = { PORTAL };
    for ( unsigned int i = 0; i < sizeof( portal_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = portal_shaped[ i ];
        block_definitions[ id ].scale = { 16, 16, 4 };
        block_definitions[ id ].offset = { 0, 0, 6 };
        block_definitions[ id ].tex_offset = { 0, -8, 0 };
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].rotate_on_placement = true;
        block_definitions[ id ].hides_self = { true, true, false };
    }
    block_definitions[ PORTAL ].breaks_in_liquid = true;

    BlockID single_chest[] = { SINGLE_CHEST_LATCH };
    for ( unsigned int i = 0; i < sizeof( single_chest ) / sizeof( BlockID ); i++ ) {
        BlockID id = single_chest[ i ];
        block_definitions[ id ].scale = { 14, 15, 14 };
        block_definitions[ id ].offset = { 1, 0, 1 };
        block_definitions[ id ].tex_offset = { -1, 0, -1 };
    }

    BlockID left_chest[] = { DOUBLE_CHEST_LEFT_LATCH };
    for ( unsigned int i = 0; i < sizeof( left_chest ) / sizeof( BlockID ); i++ ) {
        BlockID id = left_chest[ i ];
        block_definitions[ id ].scale = { 15, 15, 14 };
        block_definitions[ id ].offset = { 1, 0, 1 };
        block_definitions[ id ].tex_offset = { -1, 0, 0 };
    }
    BlockID right_chest[] = { DOUBLE_CHEST_RIGHT_LATCH };
    for ( unsigned int i = 0; i < sizeof( right_chest ) / sizeof( BlockID ); i++ ) {
        BlockID id = right_chest[ i ];
        block_definitions[ id ].scale = { 15, 15, 14 };
        block_definitions[ id ].offset = { 0, 0, 1 };
        block_definitions[ id ].tex_offset = { 0, 0, -1 };
        block_definitions[ id ].textures[ FACE_TOP ] = DOUBLE_CHEST_RIGHT_TOP;
        block_definitions[ id ].textures[ FACE_BACK ] = DOUBLE_CHEST_RIGHT_BACK;
    }

    block_definitions[ GLASS_PANE ].textures[ FACE_FRONT ] = GLASS;
    block_definitions[ GLASS_PANE ].textures[ FACE_BACK ] = GLASS;

    block_definitions[ STONE_BRICK_SLAB ].textures[ FACE_TOP ] = STONE_BRICK;
    block_definitions[ STONE_BRICK_SLAB ].textures[ FACE_BOTTOM ] = STONE_BRICK;

    block_definitions[ DARK_OAK_SLAB ].textures[ FACE_TOP ] = DARK_OAK_PLANK;
    block_definitions[ DARK_OAK_SLAB ].textures[ FACE_BOTTOM ] = DARK_OAK_PLANK;

    block_definitions[ OAK_SLAB ].textures[ FACE_TOP ] = OAK_PLANK;
    block_definitions[ OAK_SLAB ].textures[ FACE_BOTTOM ] = OAK_PLANK;

    block_definitions[ BIRTCH_SLAB ].textures[ FACE_TOP ] = BIRTCH_PLANK;
    block_definitions[ BIRTCH_SLAB ].textures[ FACE_BOTTOM ] = BIRTCH_PLANK;

    block_definitions[ JUNGLE_SLAB ].textures[ FACE_TOP ] = JUNGLE_PLANK;
    block_definitions[ JUNGLE_SLAB ].textures[ FACE_BOTTOM ] = JUNGLE_PLANK;

    block_definitions[ ACACIA_SLAB ].textures[ FACE_TOP ] = ACACIA_PLANK;
    block_definitions[ ACACIA_SLAB ].textures[ FACE_BOTTOM ] = ACACIA_PLANK;

    block_definitions[ RED_SANDSTONE_BRICK_SLAB ].textures[ FACE_TOP ] = RED_SANDSTONE_BRICK_TOP;
    block_definitions[ RED_SANDSTONE_BRICK_SLAB ].textures[ FACE_BOTTOM ] = RED_SANDSTONE_BRICK_BOTTOM;

    block_definitions[ PINE_SLAB ].textures[ FACE_TOP ] = PINE_PLANK;
    block_definitions[ PINE_SLAB ].textures[ FACE_BOTTOM ] = PINE_PLANK;

    block_definitions[ COBBLESTONE_SLAB ].textures[ FACE_TOP ] = COBBLESTONE;
    block_definitions[ COBBLESTONE_SLAB ].textures[ FACE_BOTTOM ] = COBBLESTONE;

    block_definitions[ SANDSTONE_SLAB ].textures[ FACE_TOP ] = SANDSTONE_BRICK_TOP;
    block_definitions[ SANDSTONE_SLAB ].textures[ FACE_BOTTOM ] = SANDSTONE_BRICK_BOTTOM;

    BlockID head_shaped[] = { SKELETON_HEAD, WHITHER_SKELETON_HEAD, ZOMBIE_HEAD, PLAYER_HEAD, CREEPER_HEAD };
    for ( unsigned int i = 0; i < sizeof( head_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = head_shaped[ i ];
        block_definitions[ id ].scale = { 8, 8, 8 };
        block_definitions[ id ].offset = { 4, 0, 4 };
        block_definitions[ id ].tex_offset = { -4, -4, -4 };
        // block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].rotate_on_placement = true;
        block_definitions[ id ].icon_is_isometric = false;
    }
    BlockID dust_shaped[] = { REDSTONE_LINE_1,   REDSTONE_LINE_2,   REDSTONE_CROSS,    REDSTONE_DUST_L_Q1, REDSTONE_DUST_L_Q2, REDSTONE_DUST_L_Q3, REDSTONE_DUST_L_Q4, //
                              REDSTONE_DUST_T_L, REDSTONE_DUST_T_R, REDSTONE_DUST_T_F, REDSTONE_DUST_T_B };
    for ( unsigned int i = 0; i < sizeof( dust_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = dust_shaped[ i ];
        centered_border( id, 0, 1 );
        block_definitions[ id ].is_seethrough = true;
        block_definitions[ id ].casts_shadow = false;
        block_definitions[ id ].icon_is_isometric = false;
        block_definitions[ id ].needs_place_on_any_solid[ FACE_TOP ] = true;
        block_definitions[ id ].collides_with_player = false;
        block_definitions[ id ].breaks_in_liquid = true;
        block_definitions[ id ].affected_by_redstone_power = true;
        block_definitions[ id ].transmits_redstone_power = true;

        block_definitions[ id ].textures[ FACE_LEFT ] = AIR;
        block_definitions[ id ].textures[ FACE_RIGHT ] = AIR;
        block_definitions[ id ].textures[ FACE_FRONT ] = AIR;
        block_definitions[ id ].textures[ FACE_BACK ] = AIR;
        block_definitions[ id ].textures[ FACE_BOTTOM ] = AIR;

        block_definitions[ id ].connects_to_redstone_dust = true;
        block_definitions[ id ].is_pickable = false;
        block_definitions[ id ].hides_self = { false, true, false };
    }
    block_definitions[ REDSTONE_CROSS ].is_pickable = true;
    BlockID power_dust_shaped[] = { REDSTONE_LINE_1_POWERED,   REDSTONE_LINE_2_POWERED,   REDSTONE_CROSS_POWERED,    REDSTONE_DUST_L_Q1_POWERED, REDSTONE_DUST_L_Q2_POWERED, REDSTONE_DUST_L_Q3_POWERED, REDSTONE_DUST_L_Q4_POWERED, //
                                    REDSTONE_DUST_T_L_POWERED, REDSTONE_DUST_T_R_POWERED, REDSTONE_DUST_T_F_POWERED, REDSTONE_DUST_T_B_POWERED };
    for ( unsigned int i = 0; i < sizeof( power_dust_shaped ) / sizeof( BlockID ); i++ ) {
        BlockID id = power_dust_shaped[ i ];
        block_definitions[ id ].is_pickable = false;
    }

    BlockID translucent_glass[] = {GLASS_BLACK, GLASS_BLUE, GLASS_BROWN, GLASS_LIGHT_BLUE, GLASS_GRAY, GLASS_DARK_GREEN, GLASS_LIGHT_GREEN, GLASS_TINT_GREEN, GLASS_PALE_BLUE, GLASS_WHITE};
    for ( unsigned int i = 0; i < sizeof( translucent_glass ) / sizeof( BlockID ); i++ ) {
        BlockID id = translucent_glass[ i ];
        block_definitions[ id ].renderOrder = RenderOrder_Translucent;
    }

    block_definitions[ WATER ].renderOrder = RenderOrder_Water;
    block_definitions[ WATER ].flows = 20;
    block_definitions[ LAVA ].renderOrder = RenderOrder_Water;
    block_definitions[ LAVA ].flows = 20;

    block_definitions[ GRASS ].textures[ FACE_LEFT ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_RIGHT ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_FRONT ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_BACK ] = GRASS_SIDE;
    block_definitions[ GRASS ].textures[ FACE_BOTTOM ] = DIRT;
    block_supports_random_rotations[ GRASS ] = 1.0f;
    // block_supports_random_rotations[ SAND ] = 1.0f;
    // block_supports_random_rotations[ GRAVEL ] = 1.0f;
    // block_supports_random_rotations[ DIRT ] = 1.0f;
    // block_supports_random_rotations[ LEAF ] = 1.0f;
    // block_supports_random_rotations[ WATER ] = 1.0f;
    // block_supports_random_rotations[ SNOW ] = 1.0f;

    block_definitions[ DOUBLE_SLAB ].textures[ FACE_TOP ] = SLAB_TOP;
    block_definitions[ DOUBLE_SLAB ].textures[ FACE_LEFT ] = DOUBLE_SLAB;
    block_definitions[ DOUBLE_SLAB ].textures[ FACE_BOTTOM ] = SLAB_TOP;

    block_definitions[ RED_SANDSTONE_BRICK ].textures[ FACE_TOP ] = RED_SANDSTONE_BRICK_TOP;
    block_definitions[ RED_SANDSTONE_BRICK ].textures[ FACE_BOTTOM ] = RED_SANDSTONE_BRICK_BOTTOM;

    block_definitions[ RED_SANDSTONE_BRICK_WITHER ].textures[ FACE_TOP ] = RED_SANDSTONE_BRICK_TOP;
    block_definitions[ RED_SANDSTONE_BRICK_WITHER ].textures[ FACE_BOTTOM ] = RED_SANDSTONE_BRICK_BOTTOM;

    block_definitions[ RED_SANDSTONE_BRICK_POLISHED ].textures[ FACE_TOP ] = RED_SANDSTONE_BRICK_TOP;
    block_definitions[ RED_SANDSTONE_BRICK_POLISHED ].textures[ FACE_BOTTOM ] = RED_SANDSTONE_BRICK_TOP;

    block_definitions[ SANDSTONE_BRICK ].textures[ FACE_TOP ] = SANDSTONE_BRICK_TOP;
    block_definitions[ SANDSTONE_BRICK ].textures[ FACE_BOTTOM ] = SANDSTONE_BRICK_BOTTOM;

    block_definitions[ SANDSTONE_BRICK_CREEPER ].textures[ FACE_TOP ] = SANDSTONE_BRICK_TOP;
    block_definitions[ SANDSTONE_BRICK_CREEPER ].textures[ FACE_BOTTOM ] = SANDSTONE_BRICK_BOTTOM;

    block_definitions[ SANDSTONE_BRICK_POLISHED ].textures[ FACE_TOP ] = SANDSTONE_BRICK_TOP;
    block_definitions[ SANDSTONE_BRICK_POLISHED ].textures[ FACE_BOTTOM ] = SANDSTONE_BRICK_TOP;

    block_definitions[ TNT ].textures[ FACE_TOP ] = TNT_TOP;
    block_definitions[ TNT ].textures[ FACE_BOTTOM ] = TNT_BOTTOM;

    block_definitions[ OAK_LOG ].textures[ FACE_TOP ] = OAK_LOG_TOP;
    block_definitions[ OAK_LOG ].textures[ FACE_BOTTOM ] = OAK_LOG_TOP;

    block_definitions[ BIRTCH_LOG ].textures[ FACE_TOP ] = BIRTCH_LOG_TOP;
    block_definitions[ BIRTCH_LOG ].textures[ FACE_BOTTOM ] = BIRTCH_LOG_TOP;

    block_definitions[ JUNGLE_LOG ].textures[ FACE_TOP ] = JUNGLE_LOG_TOP;
    block_definitions[ JUNGLE_LOG ].textures[ FACE_BOTTOM ] = JUNGLE_LOG_TOP;

    block_definitions[ ACACIA_LOG ].textures[ FACE_TOP ] = ACACIA_LOG_TOP;
    block_definitions[ ACACIA_LOG ].textures[ FACE_BOTTOM ] = ACACIA_LOG_TOP;

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
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_LEFT ] = DOUBLE_CHEST_RIGHT_SIDE;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].textures[ FACE_RIGHT ] = DOUBLE_CHEST_RIGHT_SIDE;
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

    block_definitions[ GLASS ].is_seethrough = true;
    block_definitions[ BIRTCH_LEAVES ].is_seethrough = true;
    block_definitions[ PINE_LEAF ].is_seethrough = true;
    block_definitions[ JUNGLE_LEAF ].is_seethrough = true;
    block_definitions[ LEAF ].is_seethrough = true;

    block_definitions[ GLASS ].casts_shadow = false;

    block_definitions[ GLASS ].no_light = NO_LIGHT_NO_DRAW;
    block_definitions[ BIRTCH_LEAVES ].no_light = NO_LIGHT_DRAW;
    block_definitions[ PINE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ JUNGLE_LEAF ].no_light = NO_LIGHT_DRAW;
    block_definitions[ LEAF ].no_light = NO_LIGHT_DRAW;

    block_definitions[ GLASS ].hides_self = { true, true, true };

    block_definitions[ FURNACE_LIT ].rotate_on_placement = true;
    block_definitions[ FURNACE_UNLIT ].rotate_on_placement = true;
    block_definitions[ SINGLE_CHEST_LATCH ].rotate_on_placement = true;
    block_definitions[ DOUBLE_CHEST_LEFT_LATCH ].rotate_on_placement = true;
    block_definitions[ DOUBLE_CHEST_RIGHT_LATCH ].rotate_on_placement = true;

    block_definitions[PORTAL].renderOrder = RenderOrder_Translucent;

    // Start flower section
    do_flowers( block_definitions );
    do_disable( block_definitions );

    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];

        // Calculate non_full_size so the result can be cached for access at runtime.
        if ( ( block->scale.x != 16 || block->scale.y != 16 || block->scale.z != 16 ) || //
             ( block->offset.x != 0 || block->offset.y != 0 || block->offset.z != 0 ) || //
             ( block->tex_offset.x != 0 || block->tex_offset.y != 0 || block->tex_offset.z != 0 ) ) {
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
            block->needs_place_on_any_solid[ FACE_TOP ] = true;
            block->icon_is_isometric = false;
            block->is_pickable = true;
            block->collides_with_player = false;
            block->breaks_in_liquid = true;
            block->affected_by_redstone_power = false;
        } else if ( block->renderOrder == RenderOrder_Water ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_BRIGHT;
            block->casts_shadow = false;
            block->icon_is_isometric = false;
            block->hides_self = { true, true, true };
            block->is_pickable = false;
            block->collides_with_player = false;
            block->can_be_placed_in = true;
        } else if ( block->renderOrder == RenderOrder_Transparent ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_DRAW;
            block->casts_shadow = false;
            block->is_pickable = false;
            block->collides_with_player = false;
            block->breaks_in_liquid = true;
            block->affected_by_redstone_power = false;
            block->can_be_placed_in = true;
        } else if ( block->renderOrder == RenderOrder_Translucent ) {
            block->is_seethrough = true;
            block->no_light = NO_LIGHT_NO_DRAW;
            block->casts_shadow = false;
            block->is_pickable = true;
            block->breaks_in_liquid = false;
            block->affected_by_redstone_power = false;
            block->can_be_placed_in = false;
        }
        // if ( block->id == LEAF ) {
        //     pr_debug( "Got" );
        // }

        block->calculated.hides_self[ FACE_TOP ] = block->hides_self.y;
        block->calculated.hides_self[ FACE_BOTTOM ] = block->hides_self.y;
        block->calculated.hides_self[ FACE_RIGHT ] = block->hides_self.x;
        block->calculated.hides_self[ FACE_FRONT ] = block->hides_self.z;
        block->calculated.hides_self[ FACE_LEFT ] = block->hides_self.x;
        block->calculated.hides_self[ FACE_BACK ] = block->hides_self.z;

        if ( block->is_seethrough ) { // glass, leaves, flowers
            block->calculated.can_mesh_x = block->scale.x == 16 ? block->hides_self.x : false;
            block->calculated.can_mesh_y = block->scale.y == 16 ? block->hides_self.y : false;
            block->calculated.can_mesh_z = block->scale.z == 16 ? block->hides_self.z : false;
        } else { // other blocks, like dirt
            block->calculated.can_mesh_x = block->scale.x == 16 ? true : false;
            block->calculated.can_mesh_y = block->scale.y == 16 ? true : false;
            block->calculated.can_mesh_z = block->scale.z == 16 ? true : false;
        }

        if ( block->is_seethrough ) { // leaves, flowers, glass
            for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
                block->calculated.is_seethrough_face[ face ] = true;
            }
        } else { // other blocks, like dirt, slabs

            block->calculated.is_seethrough_face[ FACE_TOP ] = ( block->scale.y + block->offset.y ) == 16 ? ( block->scale.x != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_BOTTOM ] = ( block->offset.y ) == 0 ? ( block->scale.x != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_RIGHT ] = ( block->scale.x + block->offset.x ) == 16 ? ( block->scale.y != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_FRONT ] = ( block->scale.z + block->offset.z ) == 16 ? ( block->scale.y != 16 || block->scale.x != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_LEFT ] = ( block->offset.x ) == 0 ? ( block->scale.y != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_BACK ] = ( block->offset.z ) == 0 ? ( block->scale.y != 16 || block->scale.x != 16 ) : true;
        }
    }
    // Block *slab_block = &block_definitions[ STONE_BRICK_SLAB ];
    // pr_debug( "Paul SLAB:%d ", slab_block->calculated.is_seethrough_face[ FACE_TOP ] );

    // Exceptions
    for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
        block_definitions[ SPIDER_WEB ].needs_place_on_any_solid[ face ] = false;
        block_definitions[ BARRIER ].needs_place_on_any_solid[ face ] = false;
        block_definitions[ DARK_BARRIER ].needs_place_on_any_solid[ face ] = false;
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
    block_definitions[ ACACIA_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ JUNGLE_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ BIRTCH_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ OAK_LOG_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ TNT_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ TNT_BOTTOM ].renderOrder = RenderOrder_Transparent;
    block_definitions[ SLAB_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ GRASS_SIDE ].renderOrder = RenderOrder_Transparent;
    block_definitions[ CRAFTING_BENCH_SIDE1 ].renderOrder = RenderOrder_Transparent;
    block_definitions[ CRAFTING_BENCH_SIDE2 ].renderOrder = RenderOrder_Transparent;
    block_definitions[ REDSTONE_LAMP_POWERED ].renderOrder = RenderOrder_Transparent;
    block_definitions[ RED_SANDSTONE_BRICK_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ RED_SANDSTONE_BRICK_BOTTOM ].renderOrder = RenderOrder_Transparent;
    block_definitions[ SANDSTONE_BRICK_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ SANDSTONE_BRICK_TOP ].renderOrder = RenderOrder_Transparent;
    block_definitions[ REDSTONE_TORCH_OFF ].renderOrder = RenderOrder_Transparent;
    block_definitions[ CAKE_SIDE ].renderOrder = RenderOrder_Transparent;
    block_definitions[ CAKE_BOTTOM ].renderOrder = RenderOrder_Transparent;
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
    block_definitions[ TURTLE_EGGS1 ].renderOrder = RenderOrder_Flowers;
    block_definitions[ TURTLE_EGGS2 ].renderOrder = RenderOrder_Flowers;
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