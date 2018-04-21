#ifndef HEADER_CONSTANTS_H
#define HEADER_CONSTANTS_H

//#define LARGE_WORLD

#ifdef LARGE_WORLD
#define MOVEMENT_SENSITIVITY 0.2f // How sensitive the arrow keys are
#define CHUNK_RADIUS_X 160 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 64 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 160 / CHUNK_SIZE
#else
#define MOVEMENT_SENSITIVITY 0.02f // How sensitive the arrow keys are
#define CHUNK_RADIUS_X 32 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 32 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 32 / CHUNK_SIZE
#endif

#define CHUNK_SIZE 8
#define NO_CLIP 1
#define MAP_SEED 42
#define LOCK_MOUSE 1

#define BLOCK_SCALE 1.0f
#define REACH_DISTANCE 7
//#define GRAVITY_STRENGTH 0.4f
#define GRAVITY_STRENGTH 0.0f

#define CAMERA_SIZE 0.01f // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 40.0f
#define DRAW_DISTANCE 1000
#define FPS_LIMIT 60.0f
#define CAMERA_FOV 70.0f
#define NUM_RENDER_THREADS 7
#define CHUNK_RENDERS_PER_FRAME ( 2 * CHUNK_RADIUS_X * 2 * CHUNK_RADIUS_Y ) // Render 1 face of the chunk cube per frame. This could be lowered to reduce studder

#define PLAYER_HEIGHT 1.85f
#define PLAYER_WIDTH 0.6f
#define PLAYER_EYE_HEIGHT 1.62f

//#define EYE_POSITION_OFFSET 0.1f
#define EYE_POSITION_OFFSET ( PLAYER_EYE_HEIGHT - ( PLAYER_HEIGHT / 2.0f ) )

#endif