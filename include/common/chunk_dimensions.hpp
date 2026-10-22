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
// wire format. CHUNK_RADIUS_XZ is the horizontal torus capacity — also the
// maximum runtime render distance. The active radius starts at
// CHUNK_RADIUS_XZ_DEFAULT (the old fixed radius) and adjusts live with =/-;
// the vertical radius is fixed at CHUNK_RADIUS_Y.
// The GLES (arm) build uses the small radii: the PowerVR board can't carry
// the desktop-size torus.
#if defined( REPGAME_LINUX ) && !defined( REPGAME_GLES )
#define CHUNK_RADIUS_XZ 32
#define CHUNK_RADIUS_XZ_DEFAULT 8
#define CHUNK_RADIUS_Y 4
#else
#define CHUNK_RADIUS_XZ 16
#define CHUNK_RADIUS_XZ_DEFAULT 3
#define CHUNK_RADIUS_Y 2
#endif

// Block array extent including the 1-cell halo shell on every side.
#define CHUNK_SIZE_INTERNAL_X ( CHUNK_SIZE_X + 2 )
#define CHUNK_SIZE_INTERNAL_Y ( CHUNK_SIZE_Y + 2 )
#define CHUNK_SIZE_INTERNAL_Z ( CHUNK_SIZE_Z + 2 )
#define CHUNK_BLOCK_SIZE ( CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Y * CHUNK_SIZE_INTERNAL_Z )
#define CHUNK_BLOCK_DRAW_START ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X )
#define CHUNK_BLOCK_DRAW_STOP ( CHUNK_BLOCK_SIZE - CHUNK_BLOCK_DRAW_START )

// Deepest mip level of the per-chunk light volume that is ever sampled:
// chunk_fragment.glsl caps light_lod at LIGHT_LOD_MAX (currently 1.0), so
// the C++ side sets GL_TEXTURE_MAX_LEVEL to this and glGenerateMipmap only
// builds the levels that can actually be read. If LIGHT_LOD_MAX is raised
// above 1.0 this must be raised to match.
#define LIGHT_TEX_MAX_MIP 1

// Distance from the camera to the farthest renderable point: the outer corner
// of the loaded chunk box (a chunk CHUNK_RADIUS_* away extends one more
// CHUNK_SIZE_* on its far side). The sky sphere encloses the world at
// SKY_RADIUS (it draws without depth testing, so its size is about
// enclosing the view, not clipping), and CAMERA_FAR_PLANE sits beyond it —
// both scale automatically with CHUNK_RADIUS_*. glm::sqrt makes these
// C++-only; shaders never reference them.
#define RENDER_DISTANCE_CORNER                                                                                        \
    ( glm::sqrt( ( float )( ( CHUNK_RADIUS_XZ + 1 ) * CHUNK_SIZE_X ) * ( ( CHUNK_RADIUS_XZ + 1 ) * CHUNK_SIZE_X ) +      \
                 ( float )( ( CHUNK_RADIUS_Y + 1 ) * CHUNK_SIZE_Y ) * ( ( CHUNK_RADIUS_Y + 1 ) * CHUNK_SIZE_Y ) +        \
                 ( float )( ( CHUNK_RADIUS_XZ + 1 ) * CHUNK_SIZE_Z ) * ( ( CHUNK_RADIUS_XZ + 1 ) * CHUNK_SIZE_Z ) ) )
#define SKY_RADIUS ( RENDER_DISTANCE_CORNER * 1.10f )
#define CAMERA_FAR_PLANE ( SKY_RADIUS * 1.15f )
