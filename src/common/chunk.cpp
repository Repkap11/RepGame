// Chunk slot lifecycle and block storage: cells/extras/edited ownership,
// get/set block access (including the all-air fold and container drop
// bookkeeping), persist, and the full release_terrain teardown. The
// worker-side load/mesh pipeline lives in chunk_load.cpp/chunk_mesh.cpp;
// render-thread GL lives in chunk_render.cpp.
#include <stdlib.h>
#include <string.h>

#include "common/Platforms.hpp" // glDeleteTextures/glDeleteQueries in release_terrain
#include "common/chunk.hpp"
#include "common/chunk_render.hpp"
#include "common/light.hpp"
#include "common/utils/map_storage.hpp"

int Chunk::get_coords_from_index( const int index, int &out_x, int &out_y, int &out_z ) {
    const int y = ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1;
    const int x = ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1;
    const int z = ( index % ( CHUNK_SIZE_INTERNAL_Z ) ) - 1;
    // return 1;
    const int result = x >= 0 && y >= 0 && z >= 0 && x < CHUNK_SIZE_X && y < CHUNK_SIZE_Y && z < CHUNK_SIZE_Z;
    out_x = x;
    out_y = y;
    out_z = z;
    return result;
}

// layers[] (chunk_render.hpp) is heap-allocated so the renderer headers
// stay out of chunk.hpp. calloc matches the old embedded-array zero state:
// num_instances 0, populated_blocks nullptr, GL objects untouched until
// ensure_gl_init calls their init(). First use allocates — real slots
// mesh (calculate_populated_blocks) before they ever draw.
void Chunk::ensure_layers( ) {
    if ( this->layers == nullptr ) {
        this->layers = static_cast<RenderLayer *>( calloc( LAST_RENDER_ORDER, sizeof( RenderLayer ) ) );
    }
}

// Recompute this slot's ChunkLoader::slot_want byte from the truth fields.
// Called by set_load_state/set_should_render and after every render-thread
// num_instances write (program/unprogram/release_terrain) so the loader's
// membership mirror can never go stale. Render thread only: slot_want is
// written here and read by rebuild_drawable_list, both render-side, while
// calculate_populated_blocks also runs on terrain workers (load_terrain) —
// its num_instances writes are covered by the program_terrain refresh that
// follows on the render thread. No-op on hand-built chunks (slot_flags
// null).
void Chunk::refresh_slot_flags( ) {
    if ( this->slot_flags == nullptr ) {
        return;
    }
    uint8_t flags = 0;
    if ( this->load_state == CHUNK_LIVE && this->should_render ) {
        flags |= SLOT_DRAWABLE;
        if ( this->layers != nullptr && //
             ( this->layers[ RenderOrder_Translucent ].num_instances != 0 || //
               this->layers[ RenderOrder_Water ].num_instances != 0 ) ) {
            flags |= SLOT_BLENDED;
        }
    }
    *this->slot_flags = flags;
}

void Chunk::destroy( ) {
    this->release_terrain( );
}

// Free every owned buffer so the slot holds no terrain data — parked chunks
// take this path (chunk_loader.cpp) so memory follows the active window
// rather than the torus capacity, and destroy() shares it. Render thread
// only (deletes GL objects); the pooled buffers go to thread-local free
// lists, so a light worker still holding a stale pointer from a just-
// finished job writes into a recycled buffer — the same pre-existing window
// as the slot-reposition path, where load_terrain frees under a worker.
void Chunk::release_terrain( ) {
    // should_render=0, occlusion-probe reset, populated_blocks frees.
    this->unprogram_terrain( );
    if ( this->layers != nullptr ) {
        for ( int renderOrder = 0; renderOrder < LAST_RENDER_ORDER; renderOrder++ ) {
            RenderLayer &layer = this->layers[ renderOrder ];
            layer.num_instances = 0;
            layer.vb_coords.destroy( );
            layer.va.destroy( );
        }
        free( this->layers );
        this->layers = nullptr;
    }
    this->refresh_slot_flags( );
    // Cleared so ensure_gl_init rebuilds the destroyed layer objects on
    // reactivation.
    this->gl_initialized = 0;
    if constexpr ( REMEMBER_BLOCKS ) {
        this->free_block_storage( );
        free( this->edited );
        this->edited = nullptr;
    }
    chunk_free_light( this->light.volume );
    this->light.volume = nullptr;
    this->light.uniform = 0;
    chunk_free_light_columns( this->light.columns );
    this->light.columns = nullptr;
    chunk_free_air_mask( this->light.air_mask );
    this->light.air_mask = nullptr;
    chunk_free_air_mask( this->light.shade_mask );
    this->light.shade_mask = nullptr;
    if ( this->light.texture ) {
        glDeleteTextures( 1, &this->light.texture );
        this->light.texture = 0;
    }
    if ( this->occl.query ) {
        glDeleteQueries( 1, &this->occl.query );
        this->occl.query = 0;
    }
    // unprogram_terrain keeps occl.pending (the query object survives a
    // reposition); here the query is gone, so the pending flag must be too.
    this->occl.pending = 0;
    // Light bookkeeping for a volume that no longer exists. light_pending_
    // listed / light_upload_listed stay whatever they were — their drain
    // loops own those flags and skip non-live chunks after clearing them.
    this->light.pending_count = 0;
    this->light.reseed = 0;
    this->light.stale_clear( );
    this->light.upload_defers = 0;
    this->light.uploaded_once = 0;
    // Staged upload in flight: bump the generation so the worker's publish
    // CAS fails and it frees its own buffer; a ready buffer (status==2) is
    // render-owned and returned to the pool. The pool lives in light.cpp.
    light_reset_staging( this );
    this->light.tex_mode = 0;
    this->light.tex_flip = 0;
    this->light.finalized.store( 0, std::memory_order_relaxed );
    this->light.write_tick.store( 0, std::memory_order_relaxed );
    this->light.dirty_box.store( LIGHT_BOX_EMPTY, std::memory_order_relaxed );
    this->needs_repopulation = 0;
    this->is_empty_chunk = true;
    // Callers persist before releasing, so nothing here is unsaved.
    this->dirty = 0;
    this->edits_read_failed = 0;
    this->applying_edits = 0;
    this->mpload_diff_ms = 0;
    this->containers.chests.clear( );
    this->containers.chests_loaded = 0;
    this->containers.furnaces.clear( );
    this->containers.furnaces_loaded = 0;
    this->containers.signs.clear( );
    this->containers.signs_loaded = 0;
    this->sidecars.mobs.clear( );
    this->sidecars.mobs_file_present = 0;
    this->sidecars.mobs_spawned = 0;
    this->sidecars.mobs_removed.clear( );
    this->sidecars.player_edited = 0;
    this->sidecars.items.clear( );
    this->sidecars.items_file_present = 0;
    this->sidecars.vehicles.clear( );
    this->sidecars.vehicles_file_present = 0;
    // Every owned buffer was dropped — invalidate any async mesh snapshot
    // taken before this release.
    this->cell_version.fetch_add( 1u, std::memory_order_relaxed );
}

BlockState Chunk::get_block( const glm::ivec3 &pos ) const {
    if constexpr ( !REMEMBER_BLOCKS ) {
        return BLOCK_STATE_AIR;
    }
    if ( this->cells == nullptr ) {
        // nullptr cells means the whole chunk is AIR.
        return BLOCK_STATE_AIR;
    }
    if ( pos.x >= CHUNK_SIZE_X + 1 || //
         pos.y >= CHUNK_SIZE_Y + 1 || //
         pos.z >= CHUNK_SIZE_Z + 1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    if ( pos.x < -1 || //
         pos.y < -1 || //
         pos.z < -1 ) {
        return BLOCK_STATE_LAST_BLOCK_ID;
    }
    return this->state_at( get_index_from_coords( pos ) );
}

// Cells/extras only — `edited` is deliberately not freed here: the
// all-air fold at the end of load_terrain calls this while the edit
// bits must survive to the next persist. Owners of `edited` are
// load_terrain (slot reuse), release_terrain, free_generated_buffers
// and inspect_load's teardown.
void Chunk::free_block_storage( ) {
    chunk_free_cells( this->cells );
    this->cells = nullptr;
    chunk_free_extras( this->extras );
    this->extras = nullptr;
}

void Chunk::free_generated_buffers( ) {
    chunk_free_cells( this->cells );
    this->cells = nullptr;
    chunk_free_extras( this->extras );
    this->extras = nullptr;
    free( this->edited );
    this->edited = nullptr;
    chunk_free_air_mask( this->light.air_mask );
    this->light.air_mask = nullptr;
    chunk_free_air_mask( this->light.shade_mask );
    this->light.shade_mask = nullptr;
}

void Chunk::set_block( const glm::ivec3 &pos, const BlockState &blockState ) const {
    if ( pos.x >= CHUNK_SIZE_X + 1 || //
         pos.y >= CHUNK_SIZE_Y + 1 || //
         pos.z >= CHUNK_SIZE_Z + 1 ) {
        return;
    }
    if ( pos.x < -1 || //
         pos.y < -1 || //
         pos.z < -1 ) {
        return;
    }
    if ( this->cells == nullptr ) {
        // All-air chunk: an AIR write changes nothing, so only materialize
        // the array when the new state actually differs from AIR.
        if ( BlockStates_equal( BLOCK_STATE_AIR, blockState ) ) {
            return;
        }
        this->cells = chunk_alloc_cells( );
        // The fresh cells[] is calloc'd = all AIR = all non-opaque, so its
        // air mask starts fully set; the write below stamps its own cell.
        if ( light_mipmaps_enabled( ) ) {
            if ( this->light.air_mask == nullptr ) {
                this->light.air_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.air_mask, 0xFF, CHUNK_BLOCK_SIZE / 8 );
            if ( this->light.shade_mask == nullptr ) {
                this->light.shade_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.shade_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        }
    }

    const int index = get_index_from_coords( pos );
    const BlockState old_state = this->state_at( index );
    if ( block_is_chest( old_state.id ) && !block_is_chest( blockState.id ) ) {
        this->containers.chests.erase( index );
    }
    if ( block_is_furnace( old_state.id ) && !block_is_furnace( blockState.id ) ) {
        this->containers.furnaces.erase( index );
    }
    if ( block_is_sign( old_state.id ) && !block_is_sign( blockState.id ) ) {
        this->containers.signs.erase( index );
    }
    this->store_state( index, block_state_normalize( old_state, blockState ) );
    light_air_mask_set( *this, index, this->id_at( index ) );
    this->mark_edited( index );
}

int Chunk::set_block_by_index_if_different( int index, const BlockState *blockState ) {
    // Defensive bounds check: index comes from the network (multiplayer chunk
    // diffs). An out-of-range value would write to arbitrary chunk memory and
    // corrupt the world. Drop the update silently rather than crash.
    if ( index < 0 || index >= CHUNK_BLOCK_SIZE ) {
        pr_debug( "Ignoring out-of-range block index:%d (max:%d)", index, CHUNK_BLOCK_SIZE );
        return 0;
    }
    if ( this->cells == nullptr ) {
        // All-air chunk: an AIR write is a no-op; anything else allocates.
        if ( BlockStates_equal( BLOCK_STATE_AIR, *blockState ) ) {
            return 0;
        }
        this->cells = chunk_alloc_cells( );
        // Same as set_block: fresh calloc'd cells[] = all non-opaque.
        if ( light_mipmaps_enabled( ) ) {
            if ( this->light.air_mask == nullptr ) {
                this->light.air_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.air_mask, 0xFF, CHUNK_BLOCK_SIZE / 8 );
            if ( this->light.shade_mask == nullptr ) {
                this->light.shade_mask = chunk_alloc_air_mask( );
            }
            memset( this->light.shade_mask, 0, CHUNK_BLOCK_SIZE / 8 );
        }
    }
    const BlockState oldBlockState = this->state_at( index );
    if ( BlockStates_equal( oldBlockState, *blockState ) ) {
        return 0;
    }
    if ( block_is_chest( oldBlockState.id ) && !block_is_chest( blockState->id ) ) {
        this->containers.chests.erase( index );
    }
    if ( block_is_furnace( oldBlockState.id ) && !block_is_furnace( blockState->id ) ) {
        this->containers.furnaces.erase( index );
    }
    if ( block_is_sign( oldBlockState.id ) && !block_is_sign( blockState->id ) ) {
        this->containers.signs.erase( index );
    }
    this->store_state( index, *blockState );
    light_air_mask_set( *this, index, blockState->id );
    this->mark_edited( index );
    this->dirty = true;
    this->needs_repopulation = true;
    // Record the change for the light engine; converted to a world-space
    // recheck seed by the lighting thread (ChunkLoader::light_drain_pending).
    if ( this->light.pending_count < static_cast<int>( sizeof( this->light.pending ) / sizeof( this->light.pending[ 0 ] ) ) ) {
        this->light.pending[ this->light.pending_count++ ] = index;
    } else {
        this->light.reseed = 1;
    }
    light_pending_enqueue( *this );
    this->is_empty_chunk = false;
    // Ensure the chunk enters the drawable list so the remeshing loop visits
    // it. An empty chunk has should_render == 0; without this, a block placed
    // in it (e.g. via a multiplayer chunk diff) wouldn't render until reload.
    this->set_should_render( 1 );
    return 1;
}

void Chunk::persist( MapStorage &map_storage ) const {
    if constexpr ( REMEMBER_BLOCKS ) {
        map_storage.persist_chunk( *this );
    }
}

