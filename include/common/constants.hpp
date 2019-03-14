#ifndef HEADER_CONSTANTS_H
#define HEADER_CONSTANTS_H

#define MOVEMENT_SENSITIVITY 0.15f // How sensitive the arrow keys are

#ifdef REPGAME_LINUX
#define CHUNK_RADIUS_X 200 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 200 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 200 / CHUNK_SIZE
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
#define CHUNK_RADIUS_X 256 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 128 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 256 / CHUNK_SIZE
#endif

#ifdef LOAD_WITH_CUDA
#define LOAD_CHUNKDS_WITH_CUDA 1
#else
#define LOAD_CHUNKDS_WITH_CUDA 0
#endif

#define PERSIST_ALL_CHUNKS 0
#define REMEMBER_BLOCKS 1
#define CULL_NON_VISIBLE 1

#define NO_CLIP 1
#define MAP_SEED 0
#define LOCK_MOUSE 1

#define BLOCK_SCALE_OFFSET 0.1f

#define DISABLE_GROUPING_BLOCKS 0
#define REACH_DISTANCE 7
#define GRAVITY_STRENGTH 0.1f
//#define GRAVITY_STRENGTH 0.0f

#define CAMERA_SIZE 0.01f // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 1.62f
#define DRAW_DISTANCE 1000
#define LIMIT_FPS 1
#define SHOW_FPS 0
#define FPS_LIMIT 60.0f
#define CAMERA_FOV 60.0f
#ifdef REPGAME_WASM
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

#endif
