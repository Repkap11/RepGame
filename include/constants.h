#ifndef HEADER_CONSTANTS_H
#define HEADER_CONSTANTS_H

//#define LARGE_WORLD

#ifdef LARGE_WORLD
#define MOVEMENT_SENSITIVITY 400.0f // How sensitive the arrow keys are
#define CHUNK_RADIUS_X 160 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 64 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 160 / CHUNK_SIZE
#else
#define MOVEMENT_SENSITIVITY 100.0f // How sensitive the arrow keys are
#define CHUNK_RADIUS_X 64 / CHUNK_SIZE
#define CHUNK_RADIUS_Y 64 / CHUNK_SIZE
#define CHUNK_RADIUS_Z 64 / CHUNK_SIZE
#endif

#define CHUNK_SIZE 8

#define BLOCK_SCALE 1.0f

#define CAMERA_SIZE 0.1f // Defines how much crop is in front (low for minecraft)
#define PERSON_HEIGHT 40.0f
#define DRAW_DISTANCE 10000.0f
#define STARTING_ANGLE_H 35.0f
#define STARTING_ANGLE_V 45.0f
#define FPS_LIMIT 60.0f
#define CAMERA_FOV 60.0f
#define NUM_RENDER_THREADS 7
#define CHUNK_RENDERS_PER_FRAME ( 2 * CHUNK_RADIUS_X * 2 * CHUNK_RADIUS_Y ) // Render 1 face of the chunk cube per frame. This could be lowered to reduce studder

#endif