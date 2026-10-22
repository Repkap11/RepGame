#pragma once

// Host-side decls for the phase-1 meshing infrastructure shared by the CPU,
// HIP, and WebGPU backends. The dialect source itself lives in
// mesh_gen_shared.hpp (tables + GPUBlockDef), mesh_gen_fns.hpp (helpers),
// and mesh_vis_logic.hpp (per-cell body).

#include "common/block_definitions.hpp" // BlockCell
#include "common/chunk.hpp"             // WorkingSpace
#include "common/mesh_gen_shared.hpp"   // GPUBlockDef

// Populates (once) and returns the host GPUBlockDef table — the compact
// per-BlockID data all phase-1 mesh backends consume. Only valid after
// block_definitions_initilize_definitions has run (first call fills it via
// call_once, so callers don't need to sequence the init themselves).
const GPUBlockDef *mesh_def_table( );

// WebGPU phase-1 meshing — implemented in src/wasm/webgpu_mapgen.cpp.
// Uploads cells, runs the shared WGSL kernel, and fills workingSpace with
// the same bytes the CPU loop writes. Returns false when the backend is
// unavailable (or on non-WASM builds), so callers fall back to CPU.
bool mesh_gen_calculate_webgpu( const BlockCell *cells, WorkingSpace *workingSpace );
