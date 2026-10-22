#pragma once

// ---------------------------------------------------------------------------
// mesh_gen_shared.hpp — single-source mesh phase-1 (per-cell visibility +
// packed lighting) shared by every meshing backend, in the wgsl_dialect.hpp
// macro dialect (same pattern as map_gen_fields*.hpp):
//
//   C++ (g++)   : MG_* expands to ordinary C++; tables are static const.
//   HIP         : same C++ expansion plus the includer's MG_QUAL (__device__).
//   WGSL        : preprocessed with -DREPGAME_WGSL_GEN; every declaration
//                 emits WGSL text into the mesh_gen.wgsl compute shader.
//
// This header is also the canonical home for the face-direction/opposite/
// rotation lookup tables and the piston face tables — block.hpp and chunk.cpp
// consume them from here rather than keeping their own copies (previously
// the HIP kernel duplicated them a third time).
//
// The per-cell algorithm itself lives in mesh_vis_logic.hpp — an
// include-inside-the-loop file like map_logic.hpp.
//
// The includer must define MG_QUAL ("static inline" on CPU, "__device__
// static" on HIP) unless preprocessing with REPGAME_WGSL_GEN.
// ---------------------------------------------------------------------------

#include "common/wgsl_dialect.hpp"
#include "common/chunk_dimensions.hpp"

// ---- Constants -------------------------------------------------------------
// Re-#defined identically to their canonical homes (constants.hpp,
// block.hpp, block_definitions.hpp): identical-token redefinition is legal
// C/C++, so the C++ build itself fails if either side drifts — no separate
// verification needed. On the WGSL pass they expand to numeric literals.
#define FACE_TOP 0
#define FACE_BOTTOM 1
#define FACE_RIGHT 2
#define FACE_FRONT 3
#define FACE_LEFT 4
#define FACE_BACK 5
#define NUM_FACES_IN_CUBE 6

#define PISTON_ROTATE_UP 4
#define PISTON_ROTATE_DOWN 5

#define BLOCK_CELL_ID_MASK 0x07FFu
#define BLOCK_CELL_ROT_SHIFT 11
#define BLOCK_CELL_ROT_MASK 0x3800u
#define BLOCK_CELL_POWERED 0x4000u
#define BLOCK_CELL_WATERLOGGED 0x8000u

#define NO_LIGHT_DRAW 0x67ffff
#define NO_LIGHT_NO_DRAW 0x6fffff
#define NO_LIGHT_BRIGHT 0

// ---- Lookup tables ---------------------------------------------------------
// Row/element value lists are macros so the C++ array and the WGSL const
// below share a single literal list — one edit updates every backend.
#define MESH_FACE_DIR_X_VALUES 0, 0, 1, 0, -1, 0
#define MESH_FACE_DIR_Y_VALUES 1, -1, 0, 0, 0, 0
#define MESH_FACE_DIR_Z_VALUES 0, 0, 0, 1, 0, -1
#define MESH_FACE_ROTATE_90_VALUES FACE_TOP, FACE_BOTTOM, FACE_BACK, FACE_RIGHT, FACE_FRONT, FACE_LEFT
#define MESH_OPPOSITE_FACE_VALUES FACE_BOTTOM, FACE_TOP, FACE_LEFT, FACE_BACK, FACE_RIGHT, FACE_FRONT
// Piston tables: world face the head end points at, and the def-space face
// an extended base still fully covers (opposite the head end).
#define MESH_PISTON_HEAD_FACES_VALUES FACE_FRONT, FACE_LEFT, FACE_BACK, FACE_RIGHT, FACE_TOP, FACE_BOTTOM
#define MESH_PISTON_COVERED_VALUES FACE_BACK, FACE_BACK, FACE_BACK, FACE_BACK, FACE_BOTTOM, FACE_TOP
// ROTATED_FACE_TABLE rows — ROTATED_FACE_TABLE[rotation][face]:
#define MESH_ROT_ROW0 FACE_TOP, FACE_BOTTOM, FACE_RIGHT, FACE_FRONT, FACE_LEFT, FACE_BACK
#define MESH_ROT_ROW1 FACE_TOP, FACE_BOTTOM, FACE_BACK, FACE_RIGHT, FACE_FRONT, FACE_LEFT
#define MESH_ROT_ROW2 FACE_TOP, FACE_BOTTOM, FACE_LEFT, FACE_BACK, FACE_RIGHT, FACE_FRONT
#define MESH_ROT_ROW3 FACE_TOP, FACE_BOTTOM, FACE_FRONT, FACE_LEFT, FACE_BACK, FACE_RIGHT

#if defined( REPGAME_WGSL_GEN )

// ---- WGSL declarations -----------------------------------------------------
// RenderOrder values — must match render_order.hpp (the C++ side
// static_asserts the enum below so reordering it breaks the build).
const RenderOrder_Transparent : u32 = 0u;
const RenderOrder_Translucent : u32 = 1u;
const RenderOrder_Water : u32 = 2u;
const RenderOrder_Flowers : u32 = 3u;
const RenderOrder_Opaque : u32 = 4u;

const FACE_DIR_X_OFFSETS : array<i32, 6> = array<i32, 6>( MESH_FACE_DIR_X_VALUES );
const FACE_DIR_Y_OFFSETS : array<i32, 6> = array<i32, 6>( MESH_FACE_DIR_Y_VALUES );
const FACE_DIR_Z_OFFSETS : array<i32, 6> = array<i32, 6>( MESH_FACE_DIR_Z_VALUES );
const FACE_ROTATE_90 : array<i32, 6> = array<i32, 6>( MESH_FACE_ROTATE_90_VALUES );
const OPPOSITE_FACE : array<i32, 6> = array<i32, 6>( MESH_OPPOSITE_FACE_VALUES );
const MESH_PISTON_HEAD_FACES : array<i32, 6> = array<i32, 6>( MESH_PISTON_HEAD_FACES_VALUES );
const MESH_PISTON_COVERED : array<i32, 6> = array<i32, 6>( MESH_PISTON_COVERED_VALUES );
const ROTATED_FACE_TABLE : array<array<i32, 6>, 4> = array<array<i32, 6>, 4>( array<i32, 6>( MESH_ROT_ROW0 ),
                                                                            array<i32, 6>( MESH_ROT_ROW1 ),
                                                                            array<i32, 6>( MESH_ROT_ROW2 ),
                                                                            array<i32, 6>( MESH_ROT_ROW3 ) );

// Compact block definition — only the fields phase-1 meshing reads. The
// kernel consumes one table entry per BlockID (LAST_BLOCK_ID entries);
// fields are u32 because WGSL has no narrower host-shareable int.
struct GPUBlockDef {
    renderOrder : u32,
    no_light : u32,
    id : u32,
    is_piston : u32,
    is_seethrough_face : array<u32, 6>,
    hides_self : array<u32, 6>,
};

#else // !REPGAME_WGSL_GEN — C++ declarations

#include "common/render_order.hpp"

static const int FACE_DIR_X_OFFSETS[ NUM_FACES_IN_CUBE ] = { MESH_FACE_DIR_X_VALUES };
static const int FACE_DIR_Y_OFFSETS[ NUM_FACES_IN_CUBE ] = { MESH_FACE_DIR_Y_VALUES };
static const int FACE_DIR_Z_OFFSETS[ NUM_FACES_IN_CUBE ] = { MESH_FACE_DIR_Z_VALUES };
static const int FACE_ROTATE_90[ NUM_FACES_IN_CUBE ] = { MESH_FACE_ROTATE_90_VALUES };
static const int OPPOSITE_FACE[ NUM_FACES_IN_CUBE ] = { MESH_OPPOSITE_FACE_VALUES };
static const int MESH_PISTON_HEAD_FACES[ NUM_FACES_IN_CUBE ] = { MESH_PISTON_HEAD_FACES_VALUES };
static const int MESH_PISTON_COVERED[ NUM_FACES_IN_CUBE ] = { MESH_PISTON_COVERED_VALUES };
static constexpr int ROTATED_FACE_TABLE[ 4 ][ NUM_FACES_IN_CUBE ] = {
    { MESH_ROT_ROW0 },
    { MESH_ROT_ROW1 },
    { MESH_ROT_ROW2 },
    { MESH_ROT_ROW3 },
};

// Pins the WGSL consts above to the real enum — if the enum order changes,
// update the WGSL block in the same edit.
static_assert( RenderOrder_Transparent == 0 && RenderOrder_Translucent == 1 && RenderOrder_Water == 2 && RenderOrder_Flowers == 3 &&
                   RenderOrder_Opaque == 4,
               "RenderOrder enum order changed — update the WGSL consts in mesh_gen_shared.hpp" );

// Compact block definition — only the fields phase-1 meshing reads. The
// same layout drives the HIP kernel table, the WebGPU storage buffer, and
// (since the dialect refactor) the CPU mesh loop itself.
struct GPUBlockDef {
    unsigned renderOrder;
    unsigned no_light;
    unsigned id; // BlockID
    unsigned is_piston;
    unsigned is_seethrough_face[ NUM_FACES_IN_CUBE ];
    unsigned hides_self[ NUM_FACES_IN_CUBE ];
};

#endif

// The shared helper functions (cell bit access, rotated/opposing-face math,
// face_is_seethrough) live in mesh_gen_fns.hpp — a separate file because it
// requires MG_QUAL, which table-only consumers (e.g. block.hpp) don't set.
