#ifndef HEADER_BLOCK_DEFINITIONS_H
#define HEADER_BLOCK_DEFINITIONS_H

#include "constants.hpp"
#include "abstract/textures.hpp"
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
    BOOK_CASE = 52,
    DARK_BARRIER = 55,
    DARK_DEAD_SAPPLING,
    GRASS_TUFT,
    CRAFTING_BENCH = 60,
    FURNACE_UNLIT,
    FURNACE_SIDE,
    WHITE_GLASS = 74,
    LEAF = 77,
    LIGHT_DEAD_SAPPLING = 80,
    GARBAGE_SAPPLING,
    CRAFTING_BENCH_SIDE1 = 84,
    CRAFTING_BENCH_SIDE2,
    FURNACE_LIT,
    FURNACE_TOP,
    DARK_OAK_SAPPLING,
    BLUE_CORAL = 91,
    FIRE,
    WATER = 95,
    RED_MUSHROOM_IN_POT,
    EMPTY_SPAWNER = 98,
    SNOW,
    SNOWY_GRASS = 101,
    REED = 106,
    MYCELIUM_SIDE = 110,
    MYCELIUM = 111,
    BIRTCH_SAPPLING = 112,
    BLUE_FLOWER = 119,
    WHEAT_0 = 129,
    WHEAT_1 = 130,
    WHEAT_2 = 131,
    WHEAT_3 = 132,
    WHEAT_4 = 133,
    WHEAT_5 = 134,
    WHEAT_6 = 135,
    WHEAT_7 = 136,
    BIRTCH_LEAVES = 140,
    MELLON_STEM = 160,
    SINGLE_CHEST_TOP = 162,
    CHEST_SIDE = 163,
    SINGLE_CHEST_LATCH = 164,
    DOUBLE_CHEST_LEFT_LATCH = 165,
    DOUBLE_CHEST_RIGHT_LATCH = 166,
    DOUBLE_CHEST_RIGHT_BACK = 167,
    DOUBLE_CHEST_LEFT_BACK = 168,
    DOUBLE_CHEST_RIGHT_TOP = 190,
    DOUBLE_CHEST_LEFT_TOP = 191,
    PINE_LEAF = 197,
    RED_FLOWER_IN_POT = 216,
    DRAGON_EGG = 248,
    PURPLE_CORAL = 260,
    RED_CORAL = 274,
    EMPTY_POT = 275,
    JUNGLE_LEAF = 293,
    CARROT_0 = 297,
    CARROT_1 = 298,
    CARROT_2 = 299,
    CARROT_3 = 300,
    CARROT_4 = 301,
    NETHER_WART_1 = 339,
    NETHER_WART_2 = 340,
    NETHER_WART_3 = 341,
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
    LARGE_WHITE_FLOWER,
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
    ACADIA_LOG_TOP = 407,
    ACADIA_LOG = 406,
    DARK_OAK_LOG_TOP = 409,
    DARK_OAK_LOG = 408,
    GRASS_TUFT4 = 460,
    BEAT = 467,
    BLUE_CORAL2 = 469,
    CACTUS_IN_POT = 543,
    PURPLE_CORAL2 = 555,
    UNDERWATER_TUFT_SHORT = 573,
    UNDERWATER_TUFT_TALL,
    PURPLE_CORAL3,
    RED_CORAL2 = 577,
    YELLOW_CORAL2 = 578,
    SEAWEED = 792,
    CORAL_SPOTS1 = 822,
    CORAL_SPOTS2 = 823,
    GRAYCALE_CORAL_1 = 852,
    GRAYCALE_CORAL_2 = 853,
    GRAYCALE_CORAL_3 = 854,
    GRAYCALE_CORAL_4 = 855,
    GRAYCALE_CORAL_5 = 856,
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
    LAST_BLOCK_ID = 1219, // Also in chunk_vertex.glsl
} BlockID;

// The GPU can only hold so many blocks, so only blocks with id's under 100 can be rotated.
// This constant is duplicated in chunk_vertex.glsl and chunk_fragment.glsl
#define MAX_ROTATABLE_BLOCK 100

typedef struct {
    BlockID id;
    BlockID textures[ NUM_FACES_IN_CUBE ];
    RenderOrder renderOrder;
    int no_light;
    int casts_shadow;
    int is_seethrough;
    int can_mesh;
    int hides_self;
    int needs_place_on_solid;
    int rotate_on_placement;
    int icon_is_isometric;
} Block;

typedef struct {
    BlockID id;
    unsigned char rotation;
    // TODO add chest furnase or other block spesific state
} BlockState;

void block_definitions_initilize_definitions( Texture *texture );
Block *block_definition_get_definition( BlockID blockID );
void block_definitions_get_random_rotations( float **out_supports_random_rotations );
bool block_definitions_is_replaced_by_neighboring_water( BlockState blockState );
void block_definitions_free_definitions( );

#endif