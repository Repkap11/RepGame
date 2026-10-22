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
    // Completion handshake: the mapAsync callback signals cv under mtx.
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    int done = 0;
    int ok = 0;
};

// Written on the main thread, read by workers — atomic for safety.
std::atomic<int> init_state{ GPU_INIT_IDLE };

pthread_mutex_t queue_mtx = PTHREAD_MUTEX_INITIALIZER;
MapGenJob *pending_head = nullptr;
MapGenJob **pending_tail = &pending_head;

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
    int ok = 0;
    if ( status == WGPUMapAsyncStatus_Success && init_state.load( std::memory_order_acquire ) == GPU_INIT_READY ) {
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
    } else {
        pr_debug( "webgpu_mapgen: mapAsync failed (status %d)", (int)status );
    }
    wgpuBufferUnmap( job->staging_buf );
    wgpuBufferRelease( job->staging_buf );
    wgpuBufferRelease( job->params_buf );
    wgpuBindGroupRelease( job->bind_group );
    pthread_mutex_lock( &job->mtx );
    job->ok = ok;
    job->done = 1;
    pthread_cond_signal( &job->cv );
    pthread_mutex_unlock( &job->mtx );
}

void submit_job( MapGenJob *job ) {
    WGPUBufferDescriptor buf_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    buf_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buf_desc.size = 16; // 3 i32s, padded
    job->params_buf = wgpuDeviceCreateBuffer( device, &buf_desc );

    buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    buf_desc.size = CELLS_BUFFER_BYTES;
    job->staging_buf = wgpuDeviceCreateBuffer( device, &buf_desc );

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
    pr_debug( "webgpu_mapgen: device error (type %d): %.*s — falling back to CPU mapgen", (int)type, (int)message.length,
              message.data ? message.data : "" );
    fail_init( "device error" );
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
    WGPURequestDeviceCallbackInfo cb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = on_request_device;
    wgpuAdapterRequestDevice( adapter, &device_desc, cb );
    wgpuAdapterRelease( adapter );
}

void begin_init( ) {
    init_state.store( GPU_INIT_PENDING, std::memory_order_release );
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

} // namespace

// Per-frame pump, called from main_loop_wasm on the browser main thread.
// Starts the async init chain once a worker has asked for the backend, and
// submits whatever jobs have queued up since the last frame.
void webgpu_mapgen_pump( ) {
    const int state = init_state.load( std::memory_order_acquire );
    if ( state == GPU_INIT_FAILED ) {
        fail_pending_jobs( );
        return;
    }
    if ( state == GPU_INIT_REQUESTED ) {
        begin_init( );
    }
    if ( init_state.load( std::memory_order_acquire ) != GPU_INIT_READY ) {
        return; // jobs stay queued until the device exists
    }

    pthread_mutex_lock( &queue_mtx );
    MapGenJob *jobs = pending_head;
    pending_head = nullptr;
    pending_tail = &pending_head;
    pthread_mutex_unlock( &queue_mtx );
    while ( jobs ) {
        MapGenJob *job = jobs;
        jobs = jobs->next;
        job->next = nullptr;
        submit_job( job );
    }
}

int MapGen::supports_webgpu( ) {
    // REPGAME_NO_WEBGPU kills the backend entirely (analogous to
    // REPGAME_NO_HIP). Resolved once per process.
    static const int disabled = getenv( "REPGAME_NO_WEBGPU" ) != nullptr;
    if ( disabled ) {
        return 0;
    }
    // The first caller arms init; the main thread's pump picks it up next
    // frame and the async adapter/device chain lands a frame or two later.
    // Until then chunks take the CPU path.
    int expected = GPU_INIT_IDLE;
    init_state.compare_exchange_strong( expected, GPU_INIT_REQUESTED, std::memory_order_acq_rel );
    return init_state.load( std::memory_order_acquire ) == GPU_INIT_READY;
}

bool MapGen::load_block_webgpu( Chunk *chunk ) {
    MapGenJob *job = new MapGenJob;
    job->chunk_pos = chunk->chunk_pos;
    job->cells = chunk->cells;

    pthread_mutex_lock( &queue_mtx );
    *pending_tail = job;
    pending_tail = &job->next;
    pthread_mutex_unlock( &queue_mtx );

    pthread_mutex_lock( &job->mtx );
    while ( !job->done ) {
        pthread_cond_wait( &job->cv, &job->mtx );
    }
    const int ok = job->ok;
    pthread_mutex_unlock( &job->mtx );
    delete job;

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
