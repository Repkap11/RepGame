#pragma once

#define MOVEMENT_SENSITIVITY_WALKING 0.0326f
#define MOVEMENT_SENSITIVITY_SPRINTING 0.125f
#define MOVEMENT_SENSITIVITY_FLYING 0.25f
#define MOVEMENT_SENSITIVITY_FLYING_SPRINTING 0.5f

#ifdef REPGAME_LINUX
#define CHUNK_SIZE_X 32
#define CHUNK_SIZE_Y 32
#define CHUNK_SIZE_Z 32
#define CHUNK_RADIUS_X ( 256 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 256 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 256 / CHUNK_SIZE_Z )
#endif

#ifdef REPGAME_WINDOWS
#define CHUNK_SIZE_X 32
#define CHUNK_SIZE_Y 32
#define CHUNK_SIZE_Z 32
#define CHUNK_RADIUS_X ( 128 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 128 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 128 / CHUNK_SIZE_Z )
#endif

#ifdef REPGAME_WASM
#define CHUNK_SIZE_X 16
#define CHUNK_SIZE_Y 16
#define CHUNK_SIZE_Z 16
#define CHUNK_RADIUS_X ( 32 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 32 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 32 / CHUNK_SIZE_Z )
#endif

#ifdef REPGAME_ANDROID
#define CHUNK_SIZE_X 32
#define CHUNK_SIZE_Y 32
#define CHUNK_SIZE_Z 32
#define CHUNK_RADIUS_X ( 128 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 128 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 128 / CHUNK_SIZE_Z )
#endif

static_assert( CHUNK_RADIUS_X > 0, "CHUNK_RADIUS_X too small" );
static_assert( CHUNK_RADIUS_Y > 0, "CHUNK_RADIUS_Y too small" );
static_assert( CHUNK_RADIUS_Z > 0, "CHUNK_RADIUS_Z too small" );


#define CHUNK_SIZE_I glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z )
#define CHUNK_SIZE_F glm::vec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z )


#ifdef REPGAME_BUILD_WITH_CUDA
#define LOAD_CHUNKS_SUPPORTS_CUDA 1
#else
#define LOAD_CHUNKS_SUPPORTS_CUDA 0
#endif

#define DEFAULT_WINDOW_WIDTH 1600
#define DEFAULT_WINDOW_HEIGHT 800

// These impact the save file size, so don't change them.
#define MAIN_INVENTORY_WIDTH 12
#define MAIN_INVENTORY_HEIGHT 6
#define HOTBAR_WIDTH 10
#define HOTBAR_HEIGHT 1

// This can be useful when debugging and you want to stop at a breakpoint.
#define ALLOW_GRAB_MOUSE 1

#define REMEMBER_BLOCKS 1
#define CULL_NON_VISIBLE 1
#define FOREST_DEBUG 0
#define DEBUG_CYCLE_AUTO_ROTATING_BLOCKS 0

#define NO_CLIP 1
#define MAP_SEED 0

#define SHOW_IMGUI 1

#define BLOCK_SCALE_OFFSET 0.2f

#define DISABLE_GROUPING_BLOCKS 0
#define REACH_DISTANCE 7

#define CAMERA_SIZE 0.01f // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 1.62f
#define DRAW_DISTANCE 1000

#define TERMINAL_VELOCITY 0.5f
#define JUMP_STRENGTH 0.07f
#define GRAVITY_STRENGTH 0.002f

#define UPS_RATE 144

// Almost the number of clients, but some fd's are not clients so it's slightly less.
#define MAX_CLIENT_FDS 100

#define CAMERA_FOV 60.0f
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#define NUM_RENDER_THREADS 1
#else
#define NUM_RENDER_THREADS 4
#endif

#define PLAYER_HEIGHT 1.85f
#define PLAYER_WIDTH 0.6f
#define PLAYER_EYE_HEIGHT 1.62f

// #define EYE_POSITION_OFFSET 0.1f
#define EYE_POSITION_OFFSET ( PLAYER_EYE_HEIGHT - ( PLAYER_HEIGHT / 2.0f ) )

#define FACE_TOP 0
#define FACE_BOTTOM 1
#define FACE_RIGHT 2
#define FACE_FRONT 3
#define FACE_LEFT 4
#define FACE_BACK 5

#define ISO_FACE_TOP 0
#define ISO_FACE_FRONT 1
#define ISO_FACE_RIGHT 2

#define BLOCK_ROTATE_0 0
#define BLOCK_ROTATE_90 1
#define BLOCK_ROTATE_180 2
#define BLOCK_ROTATE_270 3

#define NUM_FACES_IN_CUBE 6

#define CORNER_OFFSET_tfl 0
#define CORNER_OFFSET_tfr 2
#define CORNER_OFFSET_tbl 4
#define CORNER_OFFSET_tbr 6
#define CORNER_OFFSET_bfl 8
#define CORNER_OFFSET_bfr 10
#define CORNER_OFFSET_bbl 12
#define CORNER_OFFSET_bbr 14

#define CORNER_OFFSET_c 16

#define NO_LIGHT_DRAW 0x7ffff
#define NO_LIGHT_NO_DRAW 0xfffff
#define NO_LIGHT_BRIGHT 0

#define MAX_ROTATABLE_BLOCK 100