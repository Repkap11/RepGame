// WebGPU terrain-generation backend — WASM only (emdawnwebgpu port).
//
// Threading model: all wgpu calls run on the browser MAIN thread, because
// emdawnwebgpu's JS-side objects live on the thread that created them and the
// async callbacks (requestAdapter/requestDevice/mapAsync) resolve through
// that thread's JS event loop — which turns between frames. So instead of a
// dedicated GPU pthread, the device lives on the main thread and
// webgpu_mapgen_pump() runs once per frame from main_loop_wasm:
//
//   terrain worker (any pthread)          browser main thread
//   ---------------------------           -------------------
//   load_block_webgpu():
//     push Job{chunk_pos,cells}    --->   webgpu_mapgen_pump():
//     pthread_cond_wait                  params + dispatch + copy to staging
//                                        + mapAsync(AllowSpontaneous)
//                                        mapAsync callback (next JS task):
//     wakes on cv            <---          repack u32->BlockCell, signal cv
//
// A lost/absent device flips init_state to FAILED; queued jobs complete with
// ok=0 and chunk.cpp falls back to load_block_c — the same graceful-degrade
// shape as load_block_hip.
//
// Sizing mirrors hip_set_block in src/linux/hip/map_gen.hip: one invocation
// per internal (x,z) column (64x64 = 4096), each looping y.

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include <webgpu/webgpu.h>

#include "common/map_gen.hpp"
#include "common/mesh_gen.hpp"
#include "common/Logging.hpp"

#if !defined( REPGAME_WASM )
#error "webgpu_mapgen.cpp is WASM-only — it is only in src/wasm/ so this can't happen"
#endif

namespace {

enum GpuInitState {
    GPU_INIT_IDLE,        // nothing requested yet
    GPU_INIT_REQUESTED,   // a worker wants the backend; pump starts the chain
    GPU_INIT_PENDING,     // requestAdapter/requestDevice in flight
    GPU_INIT_READY,       // device + pipeline live, jobs may be submitted
    GPU_INIT_FAILED,      // no adapter/device/shader — permanent CPU fallback
};

struct MapGenJob {
    glm::ivec3 chunk_pos;
    BlockCell *cells;
    // wgpu objects held while the job is in flight (released on completion).
    WGPUBuffer params_buf = nullptr;
    WGPUBuffer staging_buf = nullptr;
    WGPUBindGroup bind_group = nullptr;
    MapGenJob *next = nullptr;
    int gen = 0; // device generation this job was submitted under
    // Completion handshake: the mapAsync callback signals cv under mtx.
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    int done = 0;
    int ok = 0;
    // Set by webgpu_mapgen_shutdown() on still-in-flight jobs: the waiting
    // worker wakes immediately and walks away WITHOUT deleting the job —
    // ownership passes to the mapAsync callback (which deletes it if it ever
    // fires; otherwise the leak dies with the page).
    int abandoned = 0;
};

// Written on the main thread, read by workers — atomic for safety.
std::atomic<int> init_state{ GPU_INIT_IDLE };
// Device generation: bumped on every init attempt. Jobs and callbacks carry
// the generation they were issued under so late-arriving callbacks from a
// dead device can't kill a fresh init.
int gpu_gen = 0;
// Bounded re-init budget: Chrome restarts a crashed GPU process, so a fresh
// requestAdapter usually succeeds — but don't retry forever.
int init_attempts_left = 3;

pthread_mutex_t queue_mtx = PTHREAD_MUTEX_INITIALIZER;
MapGenJob *pending_head = nullptr;
MapGenJob **pending_tail = &pending_head;
// Jobs submitted to wgpuBufferMapAsync and awaiting their callback. Only ever
// touched on the main thread (pump, callbacks, shutdown).
MapGenJob *inflight_head = nullptr;

void inflight_add( MapGenJob *job ) {
    job->next = inflight_head;
    inflight_head = job;
}

void inflight_remove( MapGenJob *job ) {
    for ( MapGenJob **pp = &inflight_head; *pp; pp = &( *pp )->next ) {
        if ( *pp == job ) {
            *pp = job->next;
            job->next = nullptr;
            return;
        }
    }
}

WGPUInstance instance = nullptr;
WGPUDevice device = nullptr;
WGPUQueue queue = nullptr;
WGPUComputePipeline pipeline = nullptr;
WGPUBindGroupLayout bind_group_layout = nullptr;
// Shared storage buffer — every dispatch writes the same 64^3 cell volume and
// its copy command runs before the next job's compute on the queue, so
// per-job readback is race-free.
WGPUBuffer cells_buf = nullptr;

constexpr int NUM_COLUMNS = CHUNK_SIZE_INTERNAL_X * CHUNK_SIZE_INTERNAL_Z;
constexpr uint64_t CELLS_BUFFER_BYTES = (uint64_t)CHUNK_BLOCK_SIZE * 4; // u32 per cell

char *load_wgsl_source( ) {
    FILE *f = fopen( "src/shaders/map_gen.wgsl", "rb" );
    if ( !f ) {
        pr_debug( "webgpu_mapgen: src/shaders/map_gen.wgsl missing from preload fs" );
        return nullptr;
    }
    fseek( f, 0, SEEK_END );
    const long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    char *src = (char *)malloc( size + 1 );
    if ( fread( src, 1, size, f ) != (size_t)size ) {
        pr_debug( "webgpu_mapgen: short read on map_gen.wgsl" );
        free( src );
        src = nullptr;
    } else {
        src[ size ] = '\0';
    }
    fclose( f );
    return src;
}

void fail_init( const char *what ) {
    pr_debug( "webgpu_mapgen: init failed at %s — falling back to CPU mapgen", what );
    init_state.store( GPU_INIT_FAILED, std::memory_order_release );
}

// Defined at the bottom of this namespace with the rest of the meshing code.
void begin_mesh_init( );

// Completes every queued job with ok=0 so workers fall back to CPU mapgen.
// Called on the main thread when init fails (or the device is lost).
void fail_pending_jobs( ) {
    pthread_mutex_lock( &queue_mtx );
    MapGenJob *jobs = pending_head;
    pending_head = nullptr;
    pending_tail = &pending_head;
    pthread_mutex_unlock( &queue_mtx );
    while ( jobs ) {
        MapGenJob *job = jobs;
        jobs = jobs->next;
        pthread_mutex_lock( &job->mtx );
        job->ok = 0;
        job->done = 1;
        pthread_cond_signal( &job->cv );
        pthread_mutex_unlock( &job->mtx );
    }
}

void on_map_done( WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2 ) {
    MapGenJob *job = (MapGenJob *)userdata1;
    inflight_remove( job );
    int ok = 0;
    const int state = init_state.load( std::memory_order_acquire );
    if ( status == WGPUMapAsyncStatus_Success && state == GPU_INIT_READY ) {
        // GPU_INIT_FAILED here means a device error fired between submit and
        // readback — the dispatch may never have run, so the mapped data is
        // untrustworthy (a never-written staging buffer reads as all-AIR).
        // Report failure so the caller regenerates the chunk on CPU.
        // WGSL has no u16 — the kernel wrote one u32 per cell; repack to the
        // u16 BlockCell layout (canonical state: id only, no rot/power/water).
        const uint32_t *src = (const uint32_t *)wgpuBufferGetConstMappedRange( job->staging_buf, 0, CELLS_BUFFER_BYTES );
        if ( src ) {
            for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
                job->cells[ i ] = (BlockCell)src[ i ];
            }
            ok = 1;
        }
    } else if ( job->gen == gpu_gen && state != GPU_INIT_FAILED ) {
        // An aborted map means the device tore down underneath us (GPU
        // process crash / device lost). Kill the backend so the pump stops
        // submitting into the void — otherwise every chunk enqueues a job
        // that aborts forever. Further aborted in-flight callbacks find
        // state already FAILED (or are from a dead generation) and stay
        // silent — a stale callback must not kill a fresh init.
        pr_debug( "webgpu_mapgen: mapAsync failed (status %d)", (int)status );
        fail_init( "mapAsync aborted — device lost" );
    }
    if ( job->staging_buf ) {
        wgpuBufferUnmap( job->staging_buf );
        wgpuBufferRelease( job->staging_buf );
    }
    if ( job->params_buf ) {
        wgpuBufferRelease( job->params_buf );
    }
    if ( job->bind_group ) {
        wgpuBindGroupRelease( job->bind_group );
    }
    pthread_mutex_lock( &job->mtx );
    const int abandoned = job->abandoned;
    job->ok = ok;
    job->done = 1;
    pthread_cond_signal( &job->cv );
    pthread_mutex_unlock( &job->mtx );
    if ( abandoned ) {
        // webgpu_mapgen_shutdown() already woke the worker, which walked away
        // without freeing — the callback owns the memory now.
        delete job;
    }
}

// Completes a job that never got (or lost) its GPU objects — the worker
// wakes with ok=0 and falls back to CPU mapgen.
void fail_job( MapGenJob *job ) {
    if ( job->staging_buf ) {
        wgpuBufferRelease( job->staging_buf );
        job->staging_buf = nullptr;
    }
    if ( job->params_buf ) {
        wgpuBufferRelease( job->params_buf );
        job->params_buf = nullptr;
    }
    if ( job->bind_group ) {
        wgpuBindGroupRelease( job->bind_group );
        job->bind_group = nullptr;
    }
    pthread_mutex_lock( &job->mtx );
    job->ok = 0;
    job->done = 1;
    pthread_cond_signal( &job->cv );
    pthread_mutex_unlock( &job->mtx );
}

void submit_job( MapGenJob *job ) {
    // On a lost/dead device these return null or error handles — using them
    // for mapAsync/encoding is what wrote to address zero and aborted the
    // runtime. Bail to CPU instead.
    if ( init_state.load( std::memory_order_acquire ) != GPU_INIT_READY ) {
        fail_job( job );
        return;
    }
    job->gen = gpu_gen;

    WGPUBufferDescriptor buf_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buf_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buf_desc.size = 16; // 3 i32s, padded
    job->params_buf = wgpuDeviceCreateBuffer( device, &buf_desc );

    buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    buf_desc.size = CELLS_BUFFER_BYTES;
    job->staging_buf = wgpuDeviceCreateBuffer( device, &buf_desc );
    if ( !job->params_buf || !job->staging_buf ) {
        fail_init( "buffer creation (device dead?)" );
        fail_job( job );
        return;
    }

    const int32_t params[ 3 ] = { job->chunk_pos.x * CHUNK_SIZE_X, job->chunk_pos.y * CHUNK_SIZE_Y, job->chunk_pos.z * CHUNK_SIZE_Z };
    wgpuQueueWriteBuffer( queue, job->params_buf, 0, params, sizeof( params ) );

    WGPUBindGroupEntry entries[ 2 ] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
    entries[ 0 ].binding = 0;
    entries[ 0 ].buffer = job->params_buf;
    entries[ 0 ].size = 16;
    entries[ 1 ].binding = 1;
    entries[ 1 ].buffer = cells_buf;
    entries[ 1 ].size = CELLS_BUFFER_BYTES;
    WGPUBindGroupDescriptor bg_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bg_desc.layout = bind_group_layout;
    bg_desc.entryCount = 2;
    bg_desc.entries = entries;
    job->bind_group = wgpuDeviceCreateBindGroup( device, &bg_desc );

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder( device, nullptr );
    if ( !job->bind_group || !encoder ) {
        if ( encoder ) {
            wgpuCommandEncoderRelease( encoder );
        }
        fail_init( "bind group / encoder creation (device dead?)" );
        fail_job( job );
        return;
    }
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass( encoder, nullptr );
    wgpuComputePassEncoderSetPipeline( pass, pipeline );
    wgpuComputePassEncoderSetBindGroup( pass, 0, job->bind_group, 0, nullptr );
    wgpuComputePassEncoderDispatchWorkgroups( pass, ( NUM_COLUMNS + 255 ) / 256, 1, 1 );
    wgpuComputePassEncoderEnd( pass );
    wgpuComputePassEncoderRelease( pass );
    wgpuCommandEncoderCopyBufferToBuffer( encoder, cells_buf, 0, job->staging_buf, 0, CELLS_BUFFER_BYTES );
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish( encoder, nullptr );
    wgpuCommandEncoderRelease( encoder );
    wgpuQueueSubmit( queue, 1, &commands );
    wgpuCommandBufferRelease( commands );

    inflight_add( job );
    WGPUBufferMapCallbackInfo map_cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = on_map_done;
    map_cb.userdata1 = job;
    wgpuBufferMapAsync( job->staging_buf, WGPUMapMode_Read, 0, CELLS_BUFFER_BYTES, map_cb );
}

// Dawn validation errors (invalid shader module -> invalid pipeline ->
// invalid command buffer) are reported asynchronously and do NOT surface as
// null handles — the dispatch then silently writes nothing and the staging
// readback returns zeros. Log every uncaptured error and kill the backend so
// chunks fall back to CPU rather than returning wrong terrain.
void on_uncaptured_error( const WGPUDevice *err_device, WGPUErrorType type, WGPUStringView message, void *userdata1, void *userdata2 ) {
    // userdata1 carries the generation the device was created under — a
    // stale error from a dead-generation device must not kill a fresh init.
    if ( (intptr_t)userdata1 != gpu_gen ) {
        return;
    }
    pr_debug( "webgpu_mapgen: device error (type %d): %.*s — falling back to CPU mapgen", (int)type, (int)message.length,
              message.data ? message.data : "" );
    fail_init( "device error" );
}

// Fires when the device is lost — GPU process crash, driver reset, or
// explicit destroy. This is the authoritative "GPU died" signal; the flood
// of aborted mapAsync callbacks is just downstream noise.
void on_device_lost( const WGPUDevice *lost_device, WGPUDeviceLostReason reason, WGPUStringView message, void *userdata1, void *userdata2 ) {
    if ( (intptr_t)userdata1 != gpu_gen ) {
        return; // dead-generation device — the fresh init is unaffected
    }
    pr_debug( "webgpu_mapgen: device lost (reason %d): %.*s — falling back to CPU mapgen", (int)reason, (int)message.length,
              message.data ? message.data : "" );
    fail_init( "device lost" );
}

// Pipeline creation moved here: the module handle alone can't distinguish a
// compiled shader from an invalid one — only the compilation info can.
void on_compilation_info( WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info, void *userdata1, void *userdata2 ) {
    WGPUShaderModule shader_module = (WGPUShaderModule)userdata1;
    int errors = 0;
    if ( status == WGPUCompilationInfoRequestStatus_Success && info ) {
        for ( size_t i = 0; i < info->messageCount; i++ ) {
            const WGPUCompilationMessage &m = info->messages[ i ];
            const char *kind = m.type == WGPUCompilationMessageType_Error ? "error" : ( m.type == WGPUCompilationMessageType_Warning ? "warning" : "info" );
            pr_debug( "webgpu_mapgen: WGSL %s %llu:%llu: %.*s", kind, (unsigned long long)m.lineNum, (unsigned long long)m.linePos,
                      (int)m.message.length, m.message.data ? m.message.data : "" );
            errors += ( m.type == WGPUCompilationMessageType_Error );
        }
    }
    if ( errors ) {
        wgpuShaderModuleRelease( shader_module );
        fail_init( "WGSL compile (errors above)" );
        return;
    }

    WGPUComputePipelineDescriptor pipe_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipe_desc.compute.module = shader_module;
    pipe_desc.compute.entryPoint.data = "main";
    pipe_desc.compute.entryPoint.length = WGPU_STRLEN;
    pipeline = wgpuDeviceCreateComputePipeline( device, &pipe_desc );
    wgpuShaderModuleRelease( shader_module );
    if ( !pipeline ) {
        fail_init( "compute pipeline" );
        return;
    }
    bind_group_layout = wgpuComputePipelineGetBindGroupLayout( pipeline, 0 );

    WGPUBufferDescriptor buf_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    buf_desc.size = CELLS_BUFFER_BYTES;
    cells_buf = wgpuDeviceCreateBuffer( device, &buf_desc );
    if ( !cells_buf ) {
        fail_init( "cells buffer" );
        return;
    }

    pr_debug( "webgpu_mapgen: device ready — WebGPU mapgen enabled" );
    init_state.store( GPU_INIT_READY, std::memory_order_release );
    // The device works — kick the mesh pipeline's async chain next.
    begin_mesh_init( );
}

void on_request_device( WGPURequestDeviceStatus status, WGPUDevice result_device, WGPUStringView message, void *userdata1, void *userdata2 ) {
    if ( status != WGPURequestDeviceStatus_Success || !result_device ) {
        fail_init( "requestDevice" );
        return;
    }
    device = result_device;
    queue = wgpuDeviceGetQueue( device );

    char *wgsl = load_wgsl_source( );
    if ( !wgsl ) {
        fail_init( "shader load" );
        return;
    }
    WGPUShaderSourceWGSL wgsl_src = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl_src.code.data = wgsl;
    wgsl_src.code.length = WGPU_STRLEN;
    WGPUShaderModuleDescriptor shader_desc;
    shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_src;
    shader_desc.label.data = "map_gen";
    shader_desc.label.length = WGPU_STRLEN;
    WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule( device, &shader_desc );
    free( wgsl );
    if ( !shader_module ) {
        fail_init( "shader module" );
        return;
    }

    WGPUCompilationInfoCallbackInfo info_cb = WGPU_COMPILATION_INFO_CALLBACK_INFO_INIT;
    info_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    info_cb.callback = on_compilation_info;
    info_cb.userdata1 = shader_module;
    wgpuShaderModuleGetCompilationInfo( shader_module, info_cb );
}

void on_request_adapter( WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *userdata2 ) {
    if ( status != WGPURequestAdapterStatus_Success || !adapter ) {
        fail_init( "requestAdapter (no WebGPU adapter)" );
        return;
    }
    WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_desc.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
    device_desc.uncapturedErrorCallbackInfo.userdata1 = (void *)(intptr_t)gpu_gen;
    device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    device_desc.deviceLostCallbackInfo.callback = on_device_lost;
    device_desc.deviceLostCallbackInfo.userdata1 = (void *)(intptr_t)gpu_gen;
    WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = on_request_device;
    wgpuAdapterRequestDevice( adapter, &device_desc, cb );
    wgpuAdapterRelease( adapter );
}

void begin_init( ) {
    gpu_gen++;
    init_attempts_left--;
    pr_debug( "webgpu_mapgen: init attempt %d", 3 - init_attempts_left );
    init_state.store( GPU_INIT_PENDING, std::memory_order_release );
    // A fresh instance on every attempt — any prior instance/device was
    // torn down with the GPU process; the dead handles are simply dropped.
    instance = wgpuCreateInstance( nullptr );
    if ( !instance ) {
        fail_init( "createInstance" );
        return;
    }
    WGPURequestAdapterCallbackInfo cb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = on_request_adapter;
    wgpuInstanceRequestAdapter( instance, nullptr, cb );
}

// ==========================================================================
// Phase-1 meshing — a second kernel on the same device. The shared per-cell
// logic (mesh_vis_logic.hpp) compiles to mesh_gen.wgsl; the host side mirrors
// the mapgen job/pump shape: workers enqueue + park on cv, the main thread
// submits, and on_mesh_done memcpys the readback (whose per-cell record is
// laid out identically to WorkingSpace) and signals.
// ==========================================================================

enum MeshPipeState {
    MESH_PIPE_IDLE,     // shader not requested yet
    MESH_PIPE_PENDING,  // compile/pipeline creation in flight
    MESH_PIPE_READY,    // jobs may be submitted
    MESH_PIPE_FAILED,   // shader/pipeline error — permanent CPU mesh fallback
};

struct MeshJob {
    const BlockCell *cells;      // worker-owned chunk cells; read at submit
    WorkingSpace *workingSpace;  // worker's TLS buffer; filled by callback
    WGPUBuffer staging_buf = nullptr;
    MeshJob *next = nullptr;
    int gen = 0; // device generation this job was submitted under
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    int done = 0;
    int ok = 0;
    int abandoned = 0; // see MapGenJob::abandoned
};

std::atomic<int> mesh_state{ MESH_PIPE_IDLE };
// Device generation the mesh pipeline belongs to — a re-init bumps gpu_gen
// and the dead-generation pipeline objects are dropped + rebuilt.
int mesh_state_gen = -1;

MeshJob *mesh_pending_head = nullptr;
MeshJob **mesh_pending_tail = &mesh_pending_head;
MeshJob *mesh_inflight_head = nullptr;

WGPUComputePipeline mesh_pipeline = nullptr;
// One bind group serves every job — all three buffers are shared (see the
// mapgen cells_buf comment for why per-job uploads can't race).
WGPUBindGroup mesh_bind_group = nullptr;
WGPUBuffer mesh_cells_buf = nullptr;   // Storage|CopyDst — u16 cells per job
WGPUBuffer mesh_results_buf = nullptr; // Storage|CopySrc — kernel output
WGPUBuffer mesh_defs_buf = nullptr;    // Storage — GPUBlockDef table, once

// WGSL has no u16: cells upload raw (two per u32), results read back as the
// WorkingSpace-layout record (flags u32 = the 4 bool bytes + 6 u32 lights).
constexpr uint64_t MESH_CELLS_BYTES = (uint64_t)CHUNK_BLOCK_SIZE * sizeof( BlockCell );
constexpr uint64_t MESH_RESULTS_BYTES = (uint64_t)CHUNK_BLOCK_SIZE * sizeof( WorkingSpace );
static_assert( CHUNK_BLOCK_SIZE % 2 == 0, "cells buffer must pack evenly into u32s" );
static_assert( sizeof( WorkingSpace ) == 4 + NUM_FACES_IN_CUBE * 4, "WorkingSpace layout drifted from the WGSL MeshResult" );
static_assert( sizeof( GPUBlockDef ) == 4 * 4 + 2 * NUM_FACES_IN_CUBE * 4, "GPUBlockDef layout drifted from the WGSL struct" );

char *load_mesh_wgsl_source( ) {
    FILE *f = fopen( "src/shaders/mesh_gen.wgsl", "rb" );
    if ( !f ) {
        pr_debug( "webgpu_mesh: src/shaders/mesh_gen.wgsl missing from preload fs" );
        return nullptr;
    }
    fseek( f, 0, SEEK_END );
    const long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    char *src = (char *)malloc( size + 1 );
    if ( fread( src, 1, size, f ) != (size_t)size ) {
        pr_debug( "webgpu_mesh: short read on mesh_gen.wgsl" );
        free( src );
        src = nullptr;
    } else {
        src[ size ] = '\0';
    }
    fclose( f );
    return src;
}

void mesh_fail_init( const char *what ) {
    pr_debug( "webgpu_mesh: init failed at %s — falling back to CPU meshing", what );
    mesh_state.store( MESH_PIPE_FAILED, std::memory_order_release );
}

void mesh_inflight_add( MeshJob *job ) {
    job->next = mesh_inflight_head;
    mesh_inflight_head = job;
}

void mesh_inflight_remove( MeshJob *job ) {
    for ( MeshJob **pp = &mesh_inflight_head; *pp; pp = &( *pp )->next ) {
        if ( *pp == job ) {
            *pp = job->next;
            job->next = nullptr;
            return;
        }
    }
}

void mesh_fail_job( MeshJob *job ) {
    if ( job->staging_buf ) {
        wgpuBufferRelease( job->staging_buf );
        job->staging_buf = nullptr;
    }
    pthread_mutex_lock( &job->mtx );
    job->ok = 0;
    job->done = 1;
    pthread_cond_signal( &job->cv );
    pthread_mutex_unlock( &job->mtx );
}

// Completes every queued mesh job with ok=0 so workers fall back to CPU.
void mesh_fail_pending_jobs( ) {
    pthread_mutex_lock( &queue_mtx );
    MeshJob *jobs = mesh_pending_head;
    mesh_pending_head = nullptr;
    mesh_pending_tail = &mesh_pending_head;
    pthread_mutex_unlock( &queue_mtx );
    while ( jobs ) {
        MeshJob *job = jobs;
        jobs = jobs->next;
        mesh_fail_job( job );
    }
}

void on_mesh_done( WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2 ) {
    MeshJob *job = (MeshJob *)userdata1;
    mesh_inflight_remove( job );
    int ok = 0;
    const int state = init_state.load( std::memory_order_acquire );
    if ( status == WGPUMapAsyncStatus_Success && state == GPU_INIT_READY && mesh_state.load( std::memory_order_acquire ) == MESH_PIPE_READY ) {
        // Same "don't trust mapped data after a device error" guard as the
        // mapgen callback — a dead dispatch leaves zeros, not a real mesh.
        const void *src = wgpuBufferGetConstMappedRange( job->staging_buf, 0, MESH_RESULTS_BYTES );
        if ( src ) {
            memcpy( job->workingSpace, src, MESH_RESULTS_BYTES );
            ok = 1;
        }
    } else if ( job->gen == gpu_gen && state != GPU_INIT_FAILED ) {
        pr_debug( "webgpu_mesh: mapAsync failed (status %d)", (int)status );
        fail_init( "mesh mapAsync aborted — device lost" );
    }
    if ( job->staging_buf ) {
        wgpuBufferUnmap( job->staging_buf );
        wgpuBufferRelease( job->staging_buf );
    }
    pthread_mutex_lock( &job->mtx );
    const int abandoned = job->abandoned;
    job->ok = ok;
    job->done = 1;
    pthread_cond_signal( &job->cv );
    pthread_mutex_unlock( &job->mtx );
    if ( abandoned ) {
        delete job;
    }
}

void mesh_submit_job( MeshJob *job ) {
    if ( init_state.load( std::memory_order_acquire ) != GPU_INIT_READY ||
         mesh_state.load( std::memory_order_acquire ) != MESH_PIPE_READY ) {
        mesh_fail_job( job );
        return;
    }
    job->gen = gpu_gen;

    WGPUBufferDescriptor buf_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    buf_desc.size = MESH_RESULTS_BYTES;
    job->staging_buf = wgpuDeviceCreateBuffer( device, &buf_desc );
    if ( !job->staging_buf ) {
        mesh_fail_init( "mesh staging buffer (device dead?)" );
        mesh_fail_job( job );
        return;
    }

    // Upload this chunk's cells; the queue orders the write after the
    // previous job's submitted work, so the shared input buffer is safe.
    wgpuQueueWriteBuffer( queue, mesh_cells_buf, 0, job->cells, MESH_CELLS_BYTES );

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder( device, nullptr );
    if ( !encoder ) {
        mesh_fail_init( "mesh encoder (device dead?)" );
        mesh_fail_job( job );
        return;
    }
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass( encoder, nullptr );
    wgpuComputePassEncoderSetPipeline( pass, mesh_pipeline );
    wgpuComputePassEncoderSetBindGroup( pass, 0, mesh_bind_group, 0, nullptr );
    wgpuComputePassEncoderDispatchWorkgroups( pass, ( CHUNK_BLOCK_SIZE + 255 ) / 256, 1, 1 );
    wgpuComputePassEncoderEnd( pass );
    wgpuComputePassEncoderRelease( pass );
    wgpuCommandEncoderCopyBufferToBuffer( encoder, mesh_results_buf, 0, job->staging_buf, 0, MESH_RESULTS_BYTES );
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish( encoder, nullptr );
    wgpuCommandEncoderRelease( encoder );
    wgpuQueueSubmit( queue, 1, &commands );
    wgpuCommandBufferRelease( commands );

    mesh_inflight_add( job );
    WGPUBufferMapCallbackInfo map_cb = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = on_mesh_done;
    map_cb.userdata1 = job;
    wgpuBufferMapAsync( job->staging_buf, WGPUMapMode_Read, 0, MESH_RESULTS_BYTES, map_cb );
}

// Mesh pipeline creation — a second async chain like the mapgen one,
// kicked off once the mapgen pipeline proves the device works. Runs on the
// main thread; finishes by marking mesh_state READY (or FAILED, which only
// disables meshing — mapgen keeps running).
void on_mesh_compilation_info( WGPUCompilationInfoRequestStatus status, const WGPUCompilationInfo *info, void *userdata1, void *userdata2 ) {
    WGPUShaderModule shader_module = (WGPUShaderModule)userdata1;
    int errors = 0;
    if ( status == WGPUCompilationInfoRequestStatus_Success && info ) {
        for ( size_t i = 0; i < info->messageCount; i++ ) {
            const WGPUCompilationMessage &m = info->messages[ i ];
            const char *kind = m.type == WGPUCompilationMessageType_Error ? "error" : ( m.type == WGPUCompilationMessageType_Warning ? "warning" : "info" );
            pr_debug( "webgpu_mesh: WGSL %s %llu:%llu: %.*s", kind, (unsigned long long)m.lineNum, (unsigned long long)m.linePos,
                      (int)m.message.length, m.message.data ? m.message.data : "" );
            errors += ( m.type == WGPUCompilationMessageType_Error );
        }
    }
    if ( errors ) {
        wgpuShaderModuleRelease( shader_module );
        mesh_fail_init( "mesh WGSL compile (errors above)" );
        mesh_fail_pending_jobs( );
        return;
    }

    WGPUComputePipelineDescriptor pipe_desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipe_desc.compute.module = shader_module;
    pipe_desc.compute.entryPoint.data = "main";
    pipe_desc.compute.entryPoint.length = WGPU_STRLEN;
    mesh_pipeline = wgpuDeviceCreateComputePipeline( device, &pipe_desc );
    wgpuShaderModuleRelease( shader_module );
    if ( !mesh_pipeline ) {
        mesh_fail_init( "mesh compute pipeline" );
        mesh_fail_pending_jobs( );
        return;
    }

    WGPUBufferDescriptor buf_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    buf_desc.size = MESH_CELLS_BYTES;
    mesh_cells_buf = wgpuDeviceCreateBuffer( device, &buf_desc );
    buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    buf_desc.size = MESH_RESULTS_BYTES;
    mesh_results_buf = wgpuDeviceCreateBuffer( device, &buf_desc );
    buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    buf_desc.size = LAST_BLOCK_ID * sizeof( GPUBlockDef );
    mesh_defs_buf = wgpuDeviceCreateBuffer( device, &buf_desc );
    if ( !mesh_cells_buf || !mesh_results_buf || !mesh_defs_buf ) {
        mesh_fail_init( "mesh buffers" );
        mesh_fail_pending_jobs( );
        return;
    }
    wgpuQueueWriteBuffer( queue, mesh_defs_buf, 0, mesh_def_table( ), LAST_BLOCK_ID * sizeof( GPUBlockDef ) );

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout( mesh_pipeline, 0 );
    WGPUBindGroupEntry entries[ 3 ] = { WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT };
    entries[ 0 ].binding = 0;
    entries[ 0 ].buffer = mesh_cells_buf;
    entries[ 0 ].size = MESH_CELLS_BYTES;
    entries[ 1 ].binding = 1;
    entries[ 1 ].buffer = mesh_defs_buf;
    entries[ 1 ].size = LAST_BLOCK_ID * sizeof( GPUBlockDef );
    entries[ 2 ].binding = 2;
    entries[ 2 ].buffer = mesh_results_buf;
    entries[ 2 ].size = MESH_RESULTS_BYTES;
    WGPUBindGroupDescriptor bg_desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bg_desc.layout = bgl;
    bg_desc.entryCount = 3;
    bg_desc.entries = entries;
    mesh_bind_group = wgpuDeviceCreateBindGroup( device, &bg_desc );
    if ( !mesh_bind_group ) {
        mesh_fail_init( "mesh bind group" );
        mesh_fail_pending_jobs( );
        return;
    }

    pr_debug( "webgpu_mesh: pipeline ready — WebGPU meshing enabled" );
    mesh_state.store( MESH_PIPE_READY, std::memory_order_release );
}

void begin_mesh_init( ) {
    mesh_state_gen = gpu_gen;
    mesh_state.store( MESH_PIPE_PENDING, std::memory_order_release );
    char *wgsl = load_mesh_wgsl_source( );
    if ( !wgsl ) {
        mesh_fail_init( "mesh shader load" );
        mesh_fail_pending_jobs( );
        return;
    }
    WGPUShaderSourceWGSL wgsl_src = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl_src.code.data = wgsl;
    wgsl_src.code.length = WGPU_STRLEN;
    WGPUShaderModuleDescriptor shader_desc;
    shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_src;
    shader_desc.label.data = "mesh_gen";
    shader_desc.label.length = WGPU_STRLEN;
    WGPUShaderModule shader_module = wgpuDeviceCreateShaderModule( device, &shader_desc );
    free( wgsl );
    if ( !shader_module ) {
        mesh_fail_init( "mesh shader module" );
        mesh_fail_pending_jobs( );
        return;
    }
    WGPUCompilationInfoCallbackInfo info_cb = WGPU_COMPILATION_INFO_CALLBACK_INFO_INIT;
    info_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    info_cb.callback = on_mesh_compilation_info;
    info_cb.userdata1 = shader_module;
    wgpuShaderModuleGetCompilationInfo( shader_module, info_cb );
}

} // namespace

// Exit-path safety valve, called once from main_loop_wasm before
// repgame.cleanup(). Job completion normally needs this thread's JS event
// loop (mapAsync callbacks), which never runs again once cleanup() blocks in
// TerrainLoadingThread::stop()'s pthread_join — without this, a worker
// parked on job->cv never wakes and the join wedges the renderer forever.
void webgpu_mapgen_shutdown( ) {
    init_state.store( GPU_INIT_FAILED, std::memory_order_release );
    // Workers parked on never-submitted jobs wake with ok=0.
    fail_pending_jobs( );
    // Workers parked on submitted jobs can't be completed — the mapAsync
    // callback needs the (now-dead) event loop. Mark each job abandoned and
    // signal it: the worker wakes with ok=0 and walks away without freeing;
    // the callback owns the job if it ever fires.
    for ( MapGenJob *job = inflight_head; job; job = job->next ) {
        pthread_mutex_lock( &job->mtx );
        if ( !job->done ) {
            job->abandoned = 1;
            job->ok = 0;
            job->done = 1;
            pthread_cond_signal( &job->cv );
        }
        pthread_mutex_unlock( &job->mtx );
    }
    // Same release for mesh workers: queued jobs fail, in-flight jobs are
    // abandoned to their (maybe-never-firing) callbacks.
    mesh_state.store( MESH_PIPE_FAILED, std::memory_order_release );
    mesh_fail_pending_jobs( );
    for ( MeshJob *job = mesh_inflight_head; job; job = job->next ) {
        pthread_mutex_lock( &job->mtx );
        if ( !job->done ) {
            job->abandoned = 1;
            job->ok = 0;
            job->done = 1;
            pthread_cond_signal( &job->cv );
        }
        pthread_mutex_unlock( &job->mtx );
    }
}

// Per-frame pump, called from main_loop_wasm on the browser main thread.
// Starts the async init chain once a worker has asked for the backend, and
// submits whatever jobs have queued up since the last frame.
void webgpu_mapgen_pump( ) {
    const int state = init_state.load( std::memory_order_acquire );
    if ( state == GPU_INIT_FAILED ) {
        fail_pending_jobs( );
        mesh_fail_pending_jobs( );
        // Chrome restarts a crashed GPU process, so a fresh adapter/device
        // request usually succeeds — retry while the budget lasts.
        if ( init_attempts_left > 0 ) {
            init_state.store( GPU_INIT_REQUESTED, std::memory_order_release );
        }
        return;
    }
    if ( state == GPU_INIT_REQUESTED ) {
        begin_init( );
    }
    if ( init_state.load( std::memory_order_acquire ) != GPU_INIT_READY ) {
        return; // jobs stay queued until the device exists
    }
    // A re-init bumps gpu_gen and rebuilds everything device-side — drop
    // the dead-generation mesh pipeline so it gets re-created.
    if ( mesh_state_gen != gpu_gen ) {
        mesh_state.store( MESH_PIPE_IDLE, std::memory_order_release );
        mesh_pipeline = nullptr;
        mesh_bind_group = nullptr;
        mesh_cells_buf = nullptr;
        mesh_results_buf = nullptr;
        mesh_defs_buf = nullptr;
        mesh_state_gen = gpu_gen;
    }
    if ( mesh_state.load( std::memory_order_acquire ) == MESH_PIPE_IDLE ) {
        begin_mesh_init( );
    }

    pthread_mutex_lock( &queue_mtx );
    MapGenJob *jobs = pending_head;
    pending_head = nullptr;
    pending_tail = &pending_head;
    MeshJob *mesh_jobs = mesh_state.load( std::memory_order_acquire ) == MESH_PIPE_READY ? mesh_pending_head : nullptr;
    if ( mesh_jobs ) {
        mesh_pending_head = nullptr;
        mesh_pending_tail = &mesh_pending_head;
    }
    pthread_mutex_unlock( &queue_mtx );
    while ( jobs ) {
        MapGenJob *job = jobs;
        jobs = jobs->next;
        job->next = nullptr;
        submit_job( job );
    }
    while ( mesh_jobs ) {
        MeshJob *job = mesh_jobs;
        mesh_jobs = mesh_jobs->next;
        job->next = nullptr;
        mesh_submit_job( job );
    }
    // Mesh pipeline dead for good (not just mid re-init) — drain its queue.
    if ( init_state.load( std::memory_order_acquire ) == GPU_INIT_READY &&
         mesh_state.load( std::memory_order_acquire ) == MESH_PIPE_FAILED ) {
        mesh_fail_pending_jobs( );
    }
}

int MapGen::supports_webgpu( ) {
    // REPGAME_WEBGPU=1 opts the backend in — off by default (unlike
    // REPGAME_NO_HIP's opt-out). Resolved once per process.
    static const int enabled = getenv( "REPGAME_WEBGPU" ) != nullptr && atoi( getenv( "REPGAME_WEBGPU" ) ) != 0;
    if ( !enabled ) {
        return 0;
    }
    // The first caller arms init; the main thread's pump picks it up next
    // frame and the async adapter/device chain lands a frame or two later.
    // Until then chunks take the CPU path.
    int expected = GPU_INIT_IDLE;
    init_state.compare_exchange_strong( expected, GPU_INIT_REQUESTED, std::memory_order_acq_rel );
    return init_state.load( std::memory_order_acquire ) == GPU_INIT_READY;
}

// Phase-1 meshing entry point, called from a terrain worker. Enqueues a job
// and parks on cv until the main-thread pump submits it and the mapAsync
// callback completes — or returns false immediately when the backend can
// never run, so the caller meshes on CPU instead.
bool mesh_gen_calculate_webgpu( const BlockCell *cells, WorkingSpace *workingSpace ) {
    if ( cells == nullptr || init_state.load( std::memory_order_acquire ) == GPU_INIT_FAILED ||
         mesh_state.load( std::memory_order_acquire ) == MESH_PIPE_FAILED ) {
        return false;
    }
    MeshJob *job = new MeshJob;
    job->cells = cells;
    job->workingSpace = workingSpace;

    pthread_mutex_lock( &queue_mtx );
    if ( init_state.load( std::memory_order_acquire ) == GPU_INIT_FAILED ||
         mesh_state.load( std::memory_order_acquire ) == MESH_PIPE_FAILED ) {
        // Lost/shutting down between the check and the enqueue — nobody
        // will ever complete this job.
        pthread_mutex_unlock( &queue_mtx );
        delete job;
        return false;
    }
    *mesh_pending_tail = job;
    mesh_pending_tail = &job->next;
    pthread_mutex_unlock( &queue_mtx );

    pthread_mutex_lock( &job->mtx );
    while ( !job->done ) {
        pthread_cond_wait( &job->cv, &job->mtx );
    }
    const int ok = job->ok;
    const int abandoned = job->abandoned;
    pthread_mutex_unlock( &job->mtx );
    if ( !abandoned ) {
        delete job;
    }
    return ok != 0;
}

bool MapGen::load_block_webgpu( Chunk *chunk ) {
    MapGenJob *job = new MapGenJob;
    job->chunk_pos = chunk->chunk_pos;
    job->cells = chunk->cells;

    pthread_mutex_lock( &queue_mtx );
    if ( init_state.load( std::memory_order_acquire ) == GPU_INIT_FAILED ) {
        // Device lost or shutting down — nobody will ever complete this job.
        pthread_mutex_unlock( &queue_mtx );
        delete job;
        return false;
    }
    *pending_tail = job;
    pending_tail = &job->next;
    pthread_mutex_unlock( &queue_mtx );

    pthread_mutex_lock( &job->mtx );
    while ( !job->done ) {
        pthread_cond_wait( &job->cv, &job->mtx );
    }
    const int ok = job->ok;
    const int abandoned = job->abandoned;
    pthread_mutex_unlock( &job->mtx );
    if ( !abandoned ) {
        // Abandoned jobs are owned by the (possibly never-firing) callback.
        delete job;
    }

#if MAPGEN_VERIFY
    if ( ok ) {
        // CPU-versus-WebGPU chunk diff: regenerate into cells with the CPU
        // path and compare against the GPU result kept in gpu_cells.
        BlockCell *gpu_cells = (BlockCell *)malloc( CHUNK_BLOCK_SIZE * sizeof( BlockCell ) );
        memcpy( gpu_cells, chunk->cells, CHUNK_BLOCK_SIZE * sizeof( BlockCell ) );
        MapGen::load_block_c( chunk );
        int mismatches = 0;
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            if ( gpu_cells[ i ] != chunk->cells[ i ] ) {
                if ( mismatches < 8 ) {
                    const int y = i / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X );
                    const int r = i % ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X );
                    const int x = r / CHUNK_SIZE_INTERNAL_Z;
                    const int z = r % CHUNK_SIZE_INTERNAL_Z;
                    pr_debug( "MAPGEN_VERIFY mismatch: chunk=(%d,%d,%d) cell=(%d,%d,%d) gpu=%u cpu=%u", chunk->chunk_pos.x, chunk->chunk_pos.y,
                              chunk->chunk_pos.z, x - 1, y - 1, z - 1, (unsigned)gpu_cells[ i ], (unsigned)chunk->cells[ i ] );
                }
                mismatches++;
            }
        }
        if ( mismatches ) {
            pr_debug( "MAPGEN_VERIFY: chunk=(%d,%d,%d) %d/%d cells differ", chunk->chunk_pos.x, chunk->chunk_pos.y, chunk->chunk_pos.z, mismatches,
                      CHUNK_BLOCK_SIZE );
        } else {
            // Positive confirmation a few times, then a periodic heartbeat —
            // absence of lines would otherwise be indistinguishable from
            // "WebGPU never ran".
            static int verified_chunks = 0;
            verified_chunks++;
            if ( verified_chunks <= 3 || verified_chunks % 128 == 0 ) {
                pr_debug( "MAPGEN_VERIFY ok: chunk=(%d,%d,%d) identical (verified_chunks=%d)", chunk->chunk_pos.x, chunk->chunk_pos.y,
                          chunk->chunk_pos.z, verified_chunks );
            }
        }
        memcpy( chunk->cells, gpu_cells, CHUNK_BLOCK_SIZE * sizeof( BlockCell ) );
        free( gpu_cells );
    }
#endif
    return ok;
}
