#pragma once

// Chunk geometry shared by C++ and GLSL. Shader .glsl sources are run through
// the C preprocessor (SHADER_PP in makefiles/common.mk) and #include this file,
// so it must contain ONLY preprocessor constructs — any C++ declaration would
// be emitted verbatim into the shader source. Each platform's shader rule
// passes its -DREPGAME_* flag so the matching block below is selected.

// Chunk size is identical on every platform: all clients talk to the same
// server, and the wire format indexes into this exact block layout
// (NetChunkDiffEntry.blocks_index, chunk_pos -> world coordinates).
#define CHUNK_SIZE_X 62
#define CHUNK_SIZE_Y 62
#define CHUNK_SIZE_Z 62

// Render radius is a per-platform performance choice; it does not affect the
// wire format.
#ifdef REPGAME_LINUX
#define CHUNK_RADIUS_X ( 1024 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 256 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 1024 / CHUNK_SIZE_Z )
#else
#define CHUNK_RADIUS_X ( 128 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 128 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 128 / CHUNK_SIZE_Z )
#endif

// Block array extent including the 1-cell halo shell on every side.
#define CHUNK_SIZE_INTERNAL_X ( CHUNK_SIZE_X + 2 )
#define CHUNK_SIZE_INTERNAL_Y ( CHUNK_SIZE_Y + 2 )
#define CHUNK_SIZE_INTERNAL_Z ( CHUNK_SIZE_Z + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_Z )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )

// Distance from the camera to the farthest renderable point: the outer corner
// of the loaded chunk box (a chunk CHUNK_RADIUS_* away extends one more
// CHUNK_SIZE_* on its far side). SKY_RADIUS sits just beyond it so the sky
// sphere's depth write can never clip terrain, and CAMERA_FAR_PLANE sits just
// beyond the sky — both scale automatically with CHUNK_RADIUS_*. glm::sqrt
// makes these C++-only; shaders never reference them.
#define RENDER_DISTANCE_CORNER                                                                                       \
    ( glm::sqrt( ( float )( ( CHUNK_RADIUS_X + 1 ) * CHUNK_SIZE_X ) * ( ( CHUNK_RADIUS_X + 1 ) * CHUNK_SIZE_X ) +      \
                 ( float )( ( CHUNK_RADIUS_Y + 1 ) * CHUNK_SIZE_Y ) * ( ( CHUNK_RADIUS_Y + 1 ) * CHUNK_SIZE_Y ) +      \
                 ( float )( ( CHUNK_RADIUS_Z + 1 ) * CHUNK_SIZE_Z ) * ( ( CHUNK_RADIUS_Z + 1 ) * CHUNK_SIZE_Z ) ) )
#define SKY_RADIUS ( RENDER_DISTANCE_CORNER * 1.10f )
#define CAMERA_FAR_PLANE ( SKY_RADIUS * 1.15f )
