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
#define CHUNK_RADIUS_X ( 512 / CHUNK_SIZE_X )
#define CHUNK_RADIUS_Y ( 256 / CHUNK_SIZE_Y )
#define CHUNK_RADIUS_Z ( 512 / CHUNK_SIZE_Z )
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
