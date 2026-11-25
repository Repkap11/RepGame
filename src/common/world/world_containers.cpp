#include "common/world.hpp"
#include "common/chunk.hpp"
#include "common/dimension_profile.hpp"
#include "common/chunk_loader.hpp"
#include "common/multiplayer.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/screenshot.hpp"
#include "common/Logging.hpp"
#include "common/tree_grow.hpp"
#include "common/light.hpp"
#include "common/fluid.hpp"
#include "common/imgui_overlay.hpp"
#include "common/block_update_events/FluidUpdateEvent.hpp"
#include "common/block_update_events/PistonEvent.hpp"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>


const ChestContents *World::get_chest_contents( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const auto it = chunk.containers.chests.find( Chunk::get_index_from_coords( diff ) );
        return it == chunk.containers.chests.end( ) ? nullptr : &it->second;
    }
    return nullptr;
}

ChestContents *World::get_or_create_chest_contents( const glm::ivec3 &block_pos ) {
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        if ( !block_is_chest( get_block_from_chunk( chunk, block_pos ).id ) ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const int index = Chunk::get_index_from_coords( diff );
        chunk.containers.chests_loaded = 1;
        return &chunk.containers.chests[ index ]; // default-constructs an empty chest
    }
    return nullptr;
}

bool World::set_chest_slot( const glm::ivec3 &block_pos, int slot, const InventorySlot &value ) {
    if ( slot < 0 || slot >= CHEST_SLOTS ) {
        return false;
    }
    ChestContents *contents = this->get_or_create_chest_contents( block_pos );
    if ( contents == nullptr ) {
        return false;
    }
    contents->slots[ slot ] = value;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
    return true;
}

void World::set_chest_contents( const glm::ivec3 &block_pos, const ChestContents &contents ) {
    ChestContents *dst = this->get_or_create_chest_contents( block_pos );
    if ( dst == nullptr ) {
        return;
    }
    *dst = contents;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
}

bool World::get_chest_pair( const glm::ivec3 &pos, glm::ivec3 &left_pos, glm::ivec3 &right_pos ) const {
    const BlockState state = this->get_loaded_block( pos );
    if ( !block_is_chest_half( state.id ) ) {
        return false;
    }
    const glm::ivec3 partner_pos = pos + chest_partner_offset( state.id, state.rotation );
    const BlockState partner_state = this->get_loaded_block( partner_pos );
    if ( partner_state.id != chest_partner_id( state.id ) || partner_state.rotation != state.rotation ) {
        return false;
    }
    if ( state.id == DOUBLE_CHEST_LEFT_LATCH ) {
        left_pos = pos;
        right_pos = partner_pos;
    } else {
        left_pos = partner_pos;
        right_pos = pos;
    }
    return true;
}

bool World::chest_is_unpaired( const glm::ivec3 &pos ) const {
    const BlockState state = this->get_loaded_block( pos );
    if ( !block_is_chest( state.id ) ) {
        return false;
    }
    glm::ivec3 left_pos, right_pos;
    return !this->get_chest_pair( pos, left_pos, right_pos );
}

const FurnaceContents *World::get_furnace_contents( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const auto it = chunk.containers.furnaces.find( Chunk::get_index_from_coords( diff ) );
        return it == chunk.containers.furnaces.end( ) ? nullptr : &it->second;
    }
    return nullptr;
}

FurnaceContents *World::get_or_create_furnace_contents( const glm::ivec3 &block_pos ) {
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        if ( !block_is_furnace( get_block_from_chunk( chunk, block_pos ).id ) ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const int index = Chunk::get_index_from_coords( diff );
        chunk.containers.furnaces_loaded = 1;
        this->chunkLoader.mark_slot_ticking( chunk );
        return &chunk.containers.furnaces[ index ]; // default-constructs an empty furnace
    }
    return nullptr;
}

bool World::set_furnace_slot( const glm::ivec3 &block_pos, int slot, const InventorySlot &value ) {
    if ( slot < 0 || slot >= FURNACE_INVENTORY_SLOTS ) {
        return false;
    }
    FurnaceContents *contents = this->get_or_create_furnace_contents( block_pos );
    if ( contents == nullptr ) {
        return false;
    }
    contents->slots[ slot ] = value;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
    return true;
}

void World::set_furnace_contents( const glm::ivec3 &block_pos, const FurnaceContents &contents ) {
    FurnaceContents *dst = this->get_or_create_furnace_contents( block_pos );
    if ( dst == nullptr ) {
        return;
    }
    *dst = contents;
    if ( Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        chunk_prt->dirty = 1;
    }
}

const SignText *World::get_sign_text( const glm::ivec3 &block_pos ) const {
    if ( const Chunk *chunk_prt = this->get_loaded_chunk( block_pos ) ) {
        const Chunk &chunk = *chunk_prt;
        if ( chunk.load_state ) {
            return nullptr;
        }
        const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
        const auto it = chunk.containers.signs.find( Chunk::get_index_from_coords( diff ) );
        return it == chunk.containers.signs.end( ) ? nullptr : &it->second;
    }
    return nullptr;
}

bool World::set_sign_text_impl( const glm::ivec3 &block_pos, const SignText &text, bool mark_dirty ) {
    Chunk *chunk_prt = this->get_loaded_chunk( block_pos );
    if ( chunk_prt == nullptr ) {
        return false;
    }
    Chunk &chunk = *chunk_prt;
    if ( chunk.load_state ) {
        return false;
    }
    if ( !block_is_sign( get_block_from_chunk( chunk, block_pos ).id ) ) {
        return false;
    }
    SignText clean = text;
    sign_sanitize( clean );
    const glm::ivec3 diff = block_pos - chunk.chunk_pos * CHUNK_SIZE_I;
    const int index = Chunk::get_index_from_coords( diff );
    chunk.containers.signs_loaded = 1;
    if ( clean.empty( ) ) {
        chunk.containers.signs.erase( index );
    } else {
        chunk.containers.signs[ index ] = clean;
    }
    if ( mark_dirty ) {
        chunk.dirty = 1;
    }
    return true;
}

bool World::set_sign_text( const glm::ivec3 &block_pos, const SignText &text ) {
    return this->set_sign_text_impl( block_pos, text, true );
}

bool World::set_sign_text_from_net( const glm::ivec3 &block_pos, const SignText &text ) {
    return this->set_sign_text_impl( block_pos, text, false );
}

void World::collect_signs_near( const glm::ivec3 &center, const int radius, std::vector<WorldSign> &out ) const {
    // Visit only the chunks whose block range overlaps the query box: signs
    // are rare, and scanning all MAX_LOADED_CHUNKS slots every frame just to
    // hit each chunk's map-empty test was measurable.
    const glm::ivec3 lo = glm::ivec3( glm::floor( glm::vec3( center - radius ) / CHUNK_SIZE_F ) );
    const glm::ivec3 hi = glm::ivec3( glm::floor( glm::vec3( center + radius ) / CHUNK_SIZE_F ) );
    for ( int cy = lo.y; cy <= hi.y; cy++ ) {
        for ( int cz = lo.z; cz <= hi.z; cz++ ) {
            for ( int cx = lo.x; cx <= hi.x; cx++ ) {
                const glm::ivec3 cpos( cx, cy, cz );
                Chunk &chunk = this->chunkLoader.chunkArray[ chunk_slot_from_pos( cpos ) ];
                if ( chunk.chunk_pos != cpos || chunk.load_state != CHUNK_LIVE || chunk.containers.signs.empty( ) ) {
                    continue;
                }
                const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
                for ( const auto &pair : chunk.containers.signs ) {
                    const int index = pair.first;
                    const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                                   ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                                   ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
                    const glm::ivec3 block_pos = chunk_origin + internal_pos;
                    const glm::ivec3 d = block_pos - center;
                    if ( abs( d.x ) > radius || abs( d.y ) > radius || abs( d.z ) > radius ) {
                        continue;
                    }
                    // Drop stale entries whose cell lost its sign without going
                    // through Chunk::set_block.
                    const BlockState state = chunk.state_at( index );
                    if ( !block_is_sign( state.id ) ) {
                        continue;
                    }
                    out.push_back( { block_pos, state, &pair.second } );
                }
            }
        }
    }
}

// --- Kind-keyed container API (CONTAINER_* wire family) --------------------
// One switch per call site: each arm routes to the kind's typed accessor and
// converts the blob via container_to/from_blob. New kinds add an arm here.

bool World::get_container_contents( NetContainerKind kind, const glm::ivec3 &block_pos, ContainerContents &out ) const {
    if ( net_container_kind_def( static_cast<uint8_t>( kind ) ) == nullptr ) {
        return false;
    }
    switch ( kind ) {
        case NetContainerKind::CHEST: {
            const ChestContents *c = this->get_chest_contents( block_pos );
            if ( c == nullptr ) {
                return false;
            }
            out = container_to_blob( *c );
            return true;
        }
        case NetContainerKind::FURNACE: {
            const FurnaceContents *f = this->get_furnace_contents( block_pos );
            if ( f == nullptr ) {
                return false;
            }
            out = container_to_blob( *f );
            return true;
        }
    }
    return false;
}

void World::set_container_contents( NetContainerKind kind, const glm::ivec3 &block_pos, const ContainerContents &contents ) {
    if ( net_container_kind_def( static_cast<uint8_t>( kind ) ) == nullptr ) {
        return;
    }
    switch ( kind ) {
        case NetContainerKind::CHEST: {
            ChestContents c;
            container_from_blob( contents, c );
            this->set_chest_contents( block_pos, c );
            return;
        }
        case NetContainerKind::FURNACE: {
            FurnaceContents f;
            container_from_blob( contents, f );
            this->set_furnace_contents( block_pos, f );
            return;
        }
    }
}

bool World::set_container_slot( NetContainerKind kind, const glm::ivec3 &block_pos, int slot, const InventorySlot &value ) {
    if ( net_container_kind_def( static_cast<uint8_t>( kind ) ) == nullptr ) {
        return false;
    }
    switch ( kind ) {
        case NetContainerKind::CHEST:
            return this->set_chest_slot( block_pos, slot, value );
        case NetContainerKind::FURNACE:
            return this->set_furnace_slot( block_pos, slot, value );
    }
    return false;
}

void World::tick_furnaces( ) {
    // slot_tickers limits the scan to slots that may hold tickable
    // containers (furnaces today) instead of walking all MAX_LOADED_CHUNKS:
    // set eagerly when containers appear, cleared lazily here once a LIVE
    // slot's maps are all empty.
    ChunkLoader &cl = this->chunkLoader;
    if ( cl.slot_tickers == nullptr || cl.chunkArray == nullptr ) {
        return;
    }
    for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
        if ( !cl.slot_tickers[ i ] ) {
            continue;
        }
        Chunk &chunk = cl.chunkArray[ i ];
        // No tickable container maps left: clear the byte. A future kind
        // appends `&& chunk.containers.hoppers.empty()` here.
        if ( chunk.load_state != CHUNK_LIVE || chunk.containers.furnaces.empty( ) ) {
            cl.slot_tickers[ i ] = 0;
            continue;
        }
        const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
        for ( auto &pair : chunk.containers.furnaces ) {
            const int index = pair.first;
            const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                           ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                           ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
            const glm::ivec3 block_pos = chunk_origin + internal_pos;
            // The block may have been replaced without going through
            // Chunk::set_block (e.g. an unloaded-chunk diff): drop stale
            // contents rather than ticking a phantom furnace.
            if ( !block_is_furnace( chunk.state_at( index ).id ) ) {
                pair.second.clear( );
                continue;
            }
            bool changed = false;
            const bool lit_changed = furnace_tick( pair.second, &changed );
            if ( changed ) {
                chunk.dirty = 1;
            }
            if ( lit_changed ) {
                BlockState state = chunk.state_at( index );
                state.id = state.id == FURNACE ? LIT_FURNACE : FURNACE;
                // display_id drives the mesh/light lookup — keep it canonical.
                state.display_id = state.id;
                this->set_loaded_block( block_pos, state );
            }
        }
    }
#ifdef REPGAME_DEBUG
    // Mirror-verify like rebuild_drawable_list's: a LIVE chunk with
    // tickable containers but a 0 byte would silently never tick — re-scan
    // the truth fields every so often and assert on divergence.
    static int verify_countdown = 0;
    if ( --verify_countdown <= 0 ) {
        verify_countdown = 60;
        for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
            const Chunk &chunk = cl.chunkArray[ i ];
            if ( chunk.load_state == CHUNK_LIVE && !chunk.containers.furnaces.empty( ) && !cl.slot_tickers[ i ] ) {
                pr_debug( "slot_tickers miss slot=%d pos=(%d,%d,%d)", i,
                          chunk.chunk_pos.x, chunk.chunk_pos.y, chunk.chunk_pos.z );
                assert( cl.slot_tickers[ i ] != 0 );
            }
        }
    }
#endif
}
