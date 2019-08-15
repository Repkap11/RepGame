#ifndef HEADER_CONSTANTS_H
#define HEADER_CONSTANTS_H

#define MOVEMENT_SENSITIVITY 0.15f // How sensitive the arrow keys are

#ifdef REPGAME_LINUX
#define CHUNK_RADIUS_X 200 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 200 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 200 / CHUNK_SIZE
#define CHUNK_SIZE 32
#endif

#ifdef REPGAME_WINDOWS
#define CHUNK_RADIUS_X 100 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 100 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 100 / CHUNK_SIZE
#define CHUNK_SIZE 32
#endif

#ifdef REPGAME_WASM
#define CHUNK_SIZE 32
#define CHUNK_RADIUS_X 64 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 32 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 64 / CHUNK_SIZE
#endif

#ifdef REPGAME_ANDROID
#define CHUNK_SIZE 32
#define CHUNK_RADIUS_X 200 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 200 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 200 / CHUNK_SIZE
#endif

#ifdef LOAD_WITH_CUDA
#define LOAD_CHUNKDS_WITH_CUDA 1
#else
#define LOAD_CHUNKDS_WITH_CUDA 0
#endif

#define PERSIST_ALL_CHUNKS 0
#define REMEMBER_BLOCKS 1
#define CULL_NON_VISIBLE 1
#define FOREST_DEBUG 0

#define NO_CLIP 0
#define MAP_SEED 0

#define BLOCK_SCALE_OFFSET 0.1f

#define DISABLE_GROUPING_BLOCKS 0
#define REACH_DISTANCE 7

#if NO_CLIP
#define GRAVITY_STRENGTH 0.0f
#else
#define GRAVITY_STRENGTH 0.1f
#endif

#define CAMERA_SIZE 0.01f // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 1.62f
#define DRAW_DISTANCE 1000

#ifdef REPGAME_FAST
#define LIMIT_FPS 0
#define SHOW_FPS 1
#else
#define LIMIT_FPS 1
#define SHOW_FPS 0
#endif

#define FPS_LIMIT 60.0f
#define CAMERA_FOV 60.0f
#if defined( REPGAME_WASM ) || defined( REPGAME_WINDOWS )
#define NUM_RENDER_THREADS 1
#else
#define NUM_RENDER_THREADS 7
#endif
#define CHUNK_RENDERS_PER_FRAME ( 2 * CHUNK_RADIUS_X * 2 * CHUNK_RADIUS_Y ) * 10000 // Render 1 face of the chunk cube per frame. This could be lowered to reduce studder

#define PLAYER_HEIGHT 1.85f
#define PLAYER_WIDTH 0.6f
#define PLAYER_EYE_HEIGHT 1.62f

//#define EYE_POSITION_OFFSET 0.1f
#define EYE_POSITION_OFFSET ( PLAYER_EYE_HEIGHT - ( PLAYER_HEIGHT / 2.0f ) )

#define FACE_TOP 0
#define FACE_BOTTOM 1
#define FACE_RIGHT 2
#define FACE_FRONT 3
#define FACE_LEFT 4
#define FACE_BACK 5

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
#endif
