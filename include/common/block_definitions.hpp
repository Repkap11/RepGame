#ifndef HEADER_BLOCK_DEFINITIONS_H
#define HEADER_BLOCK_DEFINITIONS_H

#include "constants.hpp"
#include "renderer/texture.hpp"
#include "common/render_order.hpp"

// According to GIMP upper left pixel coord
// X/16 + 24*(Y/16) + 1
typedef enum {
    AIR = 0,
    GRASS,
    STONE,
    DIRT,
    GRASS_SIDE,
    OAK_PLANK,
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
    OAK_LOG,
    OAK_LOG_TOP,
    IRON_BLOCK,
    GOLD_BLOCK,
    DIAMOND_BLOCK,
    EMERALD_BLOCK,
    REDSTONE_BLOCK,
    BARRIER,
    RED_MUSHROOM,
    BROWN_MUSHROOM,
    JUNGLE_SAPLING, // 39
    STONE_BRICK_SLAB = 41,
    OAK_SLAB = 42,
    GOLD_ORE = 49,
    IRON_ORE = 50,
    COAL_ORE = 51,
    BOOK_CASE = 52,
    DARK_BARRIER = 55,
    DARK_DEAD_SAPPLING,
    GRASS_TUFT,
    REDSTONE_DUST_L_Q3_POWERED = 58,
    CRAFTING_BENCH = 60,
    FURNACE_UNLIT,
    FURNACE_SIDE,
    REDSTONE_TORCH = 65,
    STONE_BUTTON = 68,
    GLASS = 74,
    LEAF = 77,
    STONE_BRICK = 79,
    LIGHT_DEAD_SAPPLING = 80,
    GARBAGE_SAPPLING,
    CRAFTING_BENCH_SIDE1 = 84,
    CRAFTING_BENCH_SIDE2,
    FURNACE_LIT,
    FURNACE_TOP,
    DARK_OAK_SAPPLING,
    BLUE_CORAL = 91,
    FIRE = 92,
    LAVA = 94,
    WATER = 95,
    RED_MUSHROOM_IN_POT = 96,
    EMPTY_SPAWNER = 98,
    SNOW,
    SNOWY_GRASS = 101,
    REED = 106,
    MYCELIUM_SIDE = 110,
    MYCELIUM = 111,
    BIRTCH_SAPPLING = 112,
    BLUE_FLOWER = 119,
    REDSTONE_DUST_L_Q4_POWERED = 120,
    TORCH = 121,
    LADDER = 124,
    WHEAT_0 = 129,
    WHEAT_1 = 130,
    WHEAT_2 = 131,
    WHEAT_3 = 132,
    WHEAT_4 = 133,
    WHEAT_5 = 134,
    WHEAT_6 = 135,
    WHEAT_7 = 136,
    BIRTCH_LEAVES = 140,
    BURNT_OUT_TORCH = 145,
    MELLON_STEM = 160,
    SINGLE_CHEST_TOP = 162,
    CHEST_SIDE = 163,
    SINGLE_CHEST_LATCH = 164,
    DOUBLE_CHEST_LEFT_LATCH = 165,
    DOUBLE_CHEST_RIGHT_LATCH = 166,
    DOUBLE_CHEST_RIGHT_BACK = 167,
    DOUBLE_CHEST_LEFT_BACK = 168,
    REDSTONE_TORCH_OFF = 172,
    CAKE = 178,
    CAKE_SIDE = 179,
    CAKE_BOTTOM = 181,
    DOUBLE_CHEST_RIGHT_TOP = 190,
    DOUBLE_CHEST_LEFT_TOP = 191,
    REDSTONE_DUST_T_F_POWERED = 192,
    PINE_LEAF = 197,
    RED_FLOWER_IN_POT = 216,
    GLASS_PANE = 221,
    REDSTONE_DUST_T_R_POWERED = 233,
    SKELETON_HEAD = 234,
    WHITHER_SKELETON_HEAD = 235,
    ZOMBIE_HEAD = 236,
    PLAYER_HEAD = 237,
    CREEPER_HEAD = 238,
    REDSTONE_DUST_L_Q1_POWERED = 246,
    DRAGON_EGG = 248,
    REDSTONE_DUST_T_L_POWERED = 249,
    REDSTONE_DUST_T_B_POWERED = 254,
    PURPLE_CORAL = 260,
    OAK_BUTTON = 262,
    SANDSTONE_BRICK_TOP = 265,
    RED_CORAL = 274,
    EMPTY_POT = 275,
    SANDSTONE_BRICK = 289,
    JUNGLE_LEAF = 293,
    PINE_PLANK = 295,
    JUNGLE_PLANK = 296,
    CARROT_0 = 297,
    CARROT_1 = 298,
    CARROT_2 = 299,
    CARROT_3 = 300,
    CARROT_4 = 301,
    SANDSTONE_BRICK_BOTTOM = 313,
    REDSTONE_LAMP_UNPOWERED = 316,
    REDSTONE_LAMP = 316,
    REDSTONE_LAMP_POWERED = 317,
    BIRTCH_PLANK = 319,
    REDSTONE_DUST_L_Q2_POWERED = 320,
    NETHER_WART_1 = 339,
    NETHER_WART_2 = 340,
    NETHER_WART_3 = 341,
    SANDSTONE_BRICK_CREEPER = 343,
    SANDSTONE_BRICK_POLISHED = 343,
    YELLOW_CORAL = 349,
    CHORUS_0 = 350,
    CHORUS_1 = 351,
    CHORUS_2 = 352,
    END_ROD = 376,
    FAT_OAK_SAPPLING = 381,
    ACACIA_SAPPLING = 382,
    POPPY_FLOWER = 385,
    LARGE_BLUE_FLOWER,
    LARGE_PURPLE_FLOWER,
    LARGE_WHITE_FLOWER = 388,
    RED_TULIP,
    ORANGE_TULIP,
    WHITE_TULIP,
    PINK_TULIP,
    SMALL_SUNFLOWER,
    LARGE_SUNFLOWER,
    SMALL_LILLIC_FLOWER,
    GRASS_TUFT2 = 398,
    GRASS_TUFT3,
    PODZEL,
    PODZEL_SIDE,
    BIRTCH_LOG_TOP = 404,
    BIRTCH_LOG = 436,
    JUNGLE_LOG_TOP = 405,
    JUNGLE_LOG = 438,
    ACACIA_LOG_TOP = 407,
    ACACIA_LOG = 406,
    DARK_OAK_LOG_TOP = 409,
    ACACIA_PLANK = 410,
    DARK_OAK_PLANK = 411,
    DARK_OAK_LOG = 408,
    PINE_BUTTON = 453,
    BIRTCH_BUTTON = 454,
    JUNGLE_BUTTON = 455,
    ACACIA_BUTTON = 456,
    DARK_OAK_BUTTON = 457,
    GRASS_TUFT4 = 460,
    BEAT = 467,
    BLUE_CORAL2 = 469,
    PINE_SLAB = 475,
    BIRTCH_SLAB = 476,
    JUNGLE_SLAB = 477,
    ACACIA_SLAB = 478,
    DARK_OAK_SLAB = 479,
    RED_SANDSTONE_BRICK = 488,
    RED_SANDSTONE_BRICK_WITHER = 489,
    RED_SANDSTONE_BRICK_POLISHED = 490,
    RED_SANDSTONE_BRICK_TOP = 491,
    RED_SANDSTONE_BRICK_BOTTOM = 492,
    RED_SANDSTONE_BRICK_SLAB = 493,
    CACTUS_IN_POT = 543,
    PURPLE_CORAL2 = 555,
    UNDERWATER_TUFT_SHORT = 573,
    UNDERWATER_TUFT_TALL,
    PURPLE_CORAL3,
    RED_CORAL2 = 577,
    YELLOW_CORAL2 = 578,
    SEAWEED = 792,
    BLACK_TORCH = 798,
    BLUE_TORCH = 809,
    PINE_PRESSURE_PLATE = 817,
    BIRTCH_PRESSURE_PLATE = 818,
    JUNGLE_PRESSURE_PLATE = 819,
    ACACIA_PRESSURE_PLATE = 820,
    DARK_OAK_PRESSURE_PLATE = 821,
    TURTLE_EGGS1 = 822,
    TURTLE_EGGS2 = 823,
    GRAYCALE_CORAL_1 = 852,
    GRAYCALE_CORAL_2 = 853,
    GRAYCALE_CORAL_3 = 854,
    GRAYCALE_CORAL_4 = 855,
    GRAYCALE_CORAL_5 = 856,
    WHITE_TORCH = 864,
    PURPLE_TORCH = 874,
    RED_TORCH = 875,
    CYAN_TORCH = 876,
    GRAYCALE_CORAL_6 = 998,
    GRAYCALE_CORAL_7,
    GRAYCALE_CORAL_8,
    GRAYCALE_CORAL_9,
    GRAYCALE_CORAL_10,
    PINECONE,
    BLUE_FLOWER2,
    WHITE_FLOWER,
    BLACK_FLOWER,
    BAMBOO = 1010,
    FLOWER_IN_POT = 1052,
    PURPLE_FLOWER_IN_POT,
    WHITE_FLOWER_IN_POT,
    BAMBOO_IN_POT,
    BIRTCH_SAPLING_IN_POT,
    BLUE_FLOWER_IN_POT,
    BROWN_MUSHROOM_IN_POT,
    BLUE_FLOWER2_IN_POT,
    YELLOW_FLOWER_IN_POT,
    OAK_SAPPLING_IN_POT,
    LIGHT_DEAD_SAPPLING_IN_POT,
    PINE_SAPPLING_IN_POT,
    DARK_OAK_SAPPLING_IN_POT,
    WHITE_FLOWER_SAPPLING_IN_POT,
    PLAYER_SELECTION = 1212,
    STEVE_HEAD_TOP,
    STEVE_HEAD_BOTTOM,
    STEVE_HEAD_LEFT,
    STEVE_HEAD_FRONT,
    STEVE_HEAD_RIGHT,
    STEVE_HEAD_BACK,
    LADDER_TOP = 1219,

    REDSTONE_DUST_T_F_UNPOWERED = 1220,
    REDSTONE_DUST_T_F = 1220,
    REDSTONE_DUST_T_R_UNPOWERED = 1221,
    REDSTONE_DUST_T_R = 1221,
    REDSTONE_DUST_T_B_UNPOWERED = 1222,
    REDSTONE_DUST_T_B = 1222,
    REDSTONE_DUST_T_L_UNPOWERED = 1223,
    REDSTONE_DUST_T_L = 1223,

    REDSTONE_CROSS_POWERED = 1225,
    REDSTONE_LINE_1_POWERED = 1226,

    REDSTONE_CROSS_UNPOWERED = 1227,
    REDSTONE_CROSS = 1227,

    REDSTONE_LINE_1_UNPOWERED = 1228,
    REDSTONE_LINE_1 = 1228,

    REDSTONE_LINE_2_POWERED = 1229,
    REDSTONE_LINE_2_UNPOWERED = 1230,
    REDSTONE_LINE_2 = 1230,

    REDSTONE_DUST_L_Q4_UNPOWERED = 1244,
    REDSTONE_DUST_L_Q4 = 1244,
    REDSTONE_DUST_L_Q3_UNPOWERED = 1245,
    REDSTONE_DUST_L_Q3 = 1245,
    REDSTONE_DUST_L_Q2_UNPOWERED = 1246,
    REDSTONE_DUST_L_Q2 = 1246,
    REDSTONE_DUST_L_Q1_UNPOWERED = 1247,
    REDSTONE_DUST_L_Q1 = 1247,

    LAST_BLOCK_ID = 1249, // Also in chunk_vertex.glsl
} BlockID;

// The GPU can only hold so many blocks, so only blocks with id's under 100 can be rotated.
// This constant is duplicated in chunk_vertex.glsl and chunk_fragment.glsl
#define MAX_ROTATABLE_BLOCK 100

typedef struct {
    BlockID id;
    BlockID textures[ NUM_FACES_IN_CUBE ];
    RenderOrder renderOrder;
    int no_light;
    bool casts_shadow;
    bool is_seethrough;
    struct {
        bool x;
        bool y;
        bool z;
    } hides_self;
    bool needs_place_on_any_solid[ NUM_FACES_IN_CUBE ];
    bool needs_place_on_solid_but_can_stack_on_self;
    bool rotate_on_placement;
    bool icon_is_isometric;
    int flows;
    struct {
        short x;
        short y;
        short z;
    } scale;
    struct {
        short x;
        short y;
        short z;
    } offset;
    struct {
        short x;
        short y;
        short z;
    } tex_offset;
    bool non_full_size;
    bool is_pickable;
    bool collides_with_player;
    bool breaks_in_liquid;
    int initial_redstone_power;
    bool affected_by_redstone_power;
    bool transmits_redstone_power;
    bool can_be_placed_in;
    struct {
        bool can_mesh_x;
        bool can_mesh_y;
        bool can_mesh_z;
        bool is_seethrough_face[ NUM_FACES_IN_CUBE ];
        bool hides_self[ NUM_FACES_IN_CUBE ];
    } calculated;
    bool connects_to_redstone_dust;
    BlockID inventory_non_isometric_id;

} Block;

// Keep in order to make save file backwards compatible
typedef struct {
    BlockID id;
    unsigned char rotation;
    int current_redstone_power;
    BlockID display_id;
} BlockState;

static BlockState BLOCK_STATE_AIR = { AIR, BLOCK_ROTATE_0, 0, AIR };
static BlockState BLOCK_STATE_LAST_BLOCK_ID = { LAST_BLOCK_ID, BLOCK_ROTATE_0, 0, LAST_BLOCK_ID };

bool BlockStates_equal( const BlockState &a, const BlockState &b );

void block_definitions_initilize_definitions( Texture *texture );
Block *block_definition_get_definition( BlockID blockID );
float *block_definitions_get_random_rotations( );
void block_definitions_free_definitions( );

#endif