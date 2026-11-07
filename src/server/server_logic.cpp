
#include "server/server_logic.hpp"
#include "common/utils/file_utils.hpp"
#include "common/item.hpp"
#include "common/RepGame.hpp"
#include "common/light.hpp"
#include "common/net/packet.hpp"
#include "common/smelting.hpp"

#include <climits>
#include <stdio.h>

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerLogic::init( const char *world_name ) {
    this->map_storage.init( world_name );
    this->entities.init( &this->map_storage );
    int64_t saved_world_time = 0;
    if ( this->map_storage.read_world_time( saved_world_time ) ) {
        this->world_time_base = saved_world_time;
    } else {
        // Fresh world starts at noon, same default as the client.
        this->world_time_base = DAY_LENGTH_TICKS / 4;
    }
}

int64_t ServerLogic::worldTimeNow( ) const {
    if ( !this->world_time_running ) {
        return this->world_time_base;
    }
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ) - this->world_time_anchor )
                                .count( );
    return this->world_time_base + static_cast<int64_t>( ( elapsed_us * UPS_RATE ) / 1000000 );
}

void ServerLogic::sendWorldTime( Server &server, int client_fd ) {
    NetWorldTimePayload p{ this->worldTimeNow( ) };
    PacketWriter w;
    net_serialize_world_time( w, p );
    server.queue_message( client_fd, NetMsgType::WORLD_TIME, 0, w.take_buf( ) );
}

void ServerLogic::broadcastWorldTime( Server &server ) {
    NetWorldTimePayload p{ this->worldTimeNow( ) };
    PacketWriter w;
    net_serialize_world_time( w, p );
    for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
        if ( server.get_data_if_client_connected( fd ) != NULL ) {
            server.queue_message( fd, NetMsgType::WORLD_TIME, 0, w.buf( ) );
        }
    }
}

void ServerLogic::on_client_connected( Server &server, int client_fd ) {
    pr_debug( "%d", client_fd );
    this->entities.on_client_connected( server, client_fd );

    // First client of a session starts the clock (it freezes again when the
    // last one leaves); everyone gets told the current time right away.
    if ( !this->world_time_running ) {
        this->world_time_running = true;
        this->world_time_anchor = std::chrono::steady_clock::now( );
        this->next_time_broadcast = this->world_time_anchor + std::chrono::seconds( WORLD_TIME_SYNC_SECONDS );
    }
    this->sendWorldTime( server, client_fd );

    // Tell the other clients about this new client
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            continue;
        }
        if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
            pr_debug( "Notifying existing player:%d of new player:%d", prop_id, client_fd );
            server.queue_empty( prop_id, NetMsgType::PLAYER_CONNECTED, client_fd );
        }
    }

    // Tell this client about the other clients already online
    for ( int online_id = 0; online_id < MAX_CLIENT_FDS; online_id++ ) {
        if ( online_id == client_fd ) {
            continue;
        }
        PacketType_DataPlayer *playerData = server.get_data_if_client_connected( online_id );
        if ( playerData != NULL ) {
            NetPlayerPayload p;
            p.x = playerData->x;
            p.y = playerData->y;
            p.z = playerData->z;
            memcpy( p.rotation, playerData->rotation, sizeof( p.rotation ) );
            p.eye_height = playerData->eye_height;
            p.flags = playerData->flags;
            PacketWriter w;
            net_serialize_player( w, p );
            server.queue_message( client_fd, NetMsgType::CLIENT_INIT, online_id, w.take_buf( ) );
            pr_debug( "Notifying new player:%d of existing player:%d", client_fd, online_id );
        }
    }
}

void ServerLogic::cancelChunkSave( const glm::ivec3 &chunk_pos ) {
    auto it = this->chunk_wake.find( chunk_pos );
    if ( it == this->chunk_wake.end( ) ) {
        return;
    }
    // Remove the old entry from wake_schedule. There may be multiple chunks
    // at the same wake time, so find the one matching chunk_pos.
    auto range = this->wake_schedule.equal_range( it->second );
    for ( auto i = range.first; i != range.second; ++i ) {
        if ( i->second == chunk_pos ) {
            this->wake_schedule.erase( i );
            break;
        }
    }
    this->chunk_wake.erase( it );
}

void ServerLogic::scheduleChunkSave( const glm::ivec3 &chunk_pos ) {
    // Cancel any pending timer for this chunk, then schedule a new one.
    this->cancelChunkSave( chunk_pos );
    auto wake = std::chrono::steady_clock::now( ) + std::chrono::seconds( CHUNK_IDLE_SAVE_SECONDS );
    this->wake_schedule.insert( { wake, chunk_pos } );
    this->chunk_wake[ chunk_pos ] = wake;
}

void ServerLogic::record_block( const glm::ivec3 &block_pos, BlockState &block_state ) {
    glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
    glm::ivec3 diff = block_pos - ( chunk_pos * CHUNK_SIZE_I );
    int block_index = Chunk::get_index_from_coords( diff );

    for ( int i = -1; i < 2; i++ ) {
        const int needs_update_x = ( ( i != 1 && diff.x == 0 ) || ( i != -1 && diff.x == ( CHUNK_SIZE_X - 1 ) ) ) || i == 0;
        if ( !needs_update_x ) {
            continue;
        }
        for ( int j = -1; j < 2; j++ ) {
            const int needs_update_y = ( ( j != 1 && diff.y == 0 ) || ( j != -1 && diff.y == ( CHUNK_SIZE_Y - 1 ) ) ) || j == 0;
            if ( !needs_update_y ) {
                continue;
            }
            for ( int k = -1; k < 2; k++ ) {
                const int needs_update_z = ( ( k != 1 && diff.z == 0 ) || ( k != -1 && diff.z == ( CHUNK_SIZE_Z - 1 ) ) ) || k == 0;
                if ( !needs_update_z ) {
                    continue;
                }

                glm::ivec3 new_chunk_pos = chunk_pos + glm::ivec3( i * needs_update_x, j * needs_update_y, k * needs_update_z );
                glm::ivec3 new_diff = block_pos - ( new_chunk_pos * CHUNK_SIZE_I );
                int new_block_index = Chunk::get_index_from_coords( new_diff );

                // pr_debug( "Updating chunk: %d %d %d  %d", new_chunk_pos.x, new_chunk_pos.y, new_chunk_pos.z, new_block_index );
                auto it_cache = this->world_cache.find( new_chunk_pos );
                // A chunk missing from the cache was idle-saved and evicted
                // (or never touched): merge its on-disk diff in first, else
                // this edit alone would overwrite the file on the next
                // persist and lose every earlier edit.
                ChunkCacheEntry &entry = *( it_cache != this->world_cache.end( ) ? &it_cache->second
                                                                               : this->loadIntoCache( new_chunk_pos ) );
                entry.blocks[ new_block_index ] = block_state;
                entry.last_edit_time = std::chrono::steady_clock::now( );
                // (Re)schedule this chunk's idle-save timer.
                this->scheduleChunkSave( new_chunk_pos );
            }
        }
    }
}

void ServerLogic::respondToChunkRequest( Server &server, int client_fd, const glm::ivec3 &chunk_offset ) {

    const auto it_world = this->world_cache.find( chunk_offset );

    // The cache holds only chunks with unsaved edits; a hit is newer than
    // the save file. Unedited chunks stream their diffs straight from disk
    // without being cached.
    std::map<int, BlockState> disk_diffs;
    const std::map<int, BlockState> *chunk_cache_prt;
    if ( it_world != this->world_cache.end( ) ) {
        chunk_cache_prt = &it_world->second.blocks;
    } else {
        if ( this->known_empty_chunks.count( chunk_offset ) ) {
            // Known to have no diffs — skip the disk entirely.
            return;
        }
        this->loadDiffsFromDisk( chunk_offset, disk_diffs );
        if ( disk_diffs.empty( ) ) {
            if ( this->known_empty_chunks.size( ) >= KNOWN_EMPTY_CHUNKS_MAX ) {
                this->known_empty_chunks.clear( );
            }
            this->known_empty_chunks.insert( chunk_offset );
        }
        chunk_cache_prt = &disk_diffs;
    }
    const std::map<int, BlockState> &chunk_cache = *chunk_cache_prt;
    if ( chunk_cache.empty( ) ) {
        // No diffs for this chunk; emit no frame.
        return;
    }

    NetChunkDiffResultPayload diff;
    diff.chunk_x = chunk_offset.x;
    diff.chunk_y = chunk_offset.y;
    diff.chunk_z = chunk_offset.z;
    diff.diffs.clear( );
    diff.diffs.reserve( chunk_cache.size( ) );
    for ( auto [ blocks_index, blockState ] : chunk_cache ) {
        NetChunkDiffEntry entry;
        entry.blocks_index = static_cast<uint32_t>( blocks_index );
        entry.blockState = blockState;
        diff.diffs.push_back( entry );
    }
    diff.num_diffs = static_cast<uint32_t>( diff.diffs.size( ) );

    PacketWriter w;
    net_serialize_chunk_diff_result( w, diff );
    server.queue_message( client_fd, NetMsgType::CHUNK_DIFF_RESULT, 0, w.take_buf( ) );
}

// ---------------------------------------------------------------------------
// Chests: contents live in a per-chunk sidecar file, cached here per chunk and
// shared (with a subscriber set per chest) across clients. The model is
// optimistic per-slot: the server trusts a subscribed client's slot writes,
// broadcasts them to the other viewers, and snapshots the whole chest to a
// client that breaks a chest so it can loot the items.
// ---------------------------------------------------------------------------

ChestChunkState &ServerLogic::ensureChestChunk( const glm::ivec3 &chunk_pos ) {
    ChestChunkState &cs = this->chest_chunks[ chunk_pos ];
    if ( !cs.loaded ) {
        cs.loaded = true;
        this->map_storage.load_chests( chunk_pos, cs.chests );
    }
    return cs;
}

void ServerLogic::persistChestChunk( const glm::ivec3 &chunk_pos ) {
    auto it = this->chest_chunks.find( chunk_pos );
    if ( it == this->chest_chunks.end( ) || !it->second.dirty ) {
        return;
    }
    this->map_storage.persist_chests( chunk_pos, it->second.chests );
    it->second.dirty = false;
}

bool ServerLogic::chestChunkHasSubscribers( const glm::ivec3 &chunk_pos ) const {
    const auto cit = this->chest_chunks.find( chunk_pos );
    if ( cit == this->chest_chunks.end( ) ) {
        return false;
    }
    for ( const auto &pair : cit->second.chests ) {
        const auto sit = this->chest_subscribers.find( pair.first );
        if ( sit != this->chest_subscribers.end( ) && !sit->second.empty( ) ) {
            return true;
        }
    }
    return false;
}

void ServerLogic::sendChestContents( Server &server, int client_fd, const glm::ivec3 &chest_pos, const ChestContents &contents ) {
    NetChestContentsPayload p;
    p.x = chest_pos.x;
    p.y = chest_pos.y;
    p.z = chest_pos.z;
    p.contents = contents;
    PacketWriter w;
    net_serialize_chest_contents( w, p );
    server.queue_message( client_fd, NetMsgType::CHEST_CONTENTS, 0, w.take_buf( ) );
}

void ServerLogic::unsubscribeChest( int client_fd ) {
    const auto it = this->client_open_chest.find( client_fd );
    if ( it == this->client_open_chest.end( ) ) {
        return;
    }
    const glm::ivec3 chest_pos = it->second;
    this->client_open_chest.erase( it );
    const auto sit = this->chest_subscribers.find( chest_pos );
    if ( sit != this->chest_subscribers.end( ) ) {
        sit->second.erase( client_fd );
        if ( sit->second.empty( ) ) {
            this->chest_subscribers.erase( sit );
        }
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( chest_pos ) / CHUNK_SIZE_F );
    if ( !this->chestChunkHasSubscribers( chunk_pos ) ) {
        // Last viewer closed: evict an unmodified cache now, or let the idle
        // timer write+evict a dirty one.
        const auto cit = this->chest_chunks.find( chunk_pos );
        if ( cit != this->chest_chunks.end( ) && !cit->second.dirty ) {
            this->chest_chunks.erase( cit );
        } else if ( cit != this->chest_chunks.end( ) ) {
            this->scheduleChunkSave( chunk_pos );
        }
    }
}

void ServerLogic::handleChestOpen( Server &server, int client_fd, const glm::ivec3 &chest_pos ) {
    // A client can only have one chest open; swap subscriptions.
    this->unsubscribeChest( client_fd );
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( chest_pos ) / CHUNK_SIZE_F );
    ChestChunkState &cs = this->ensureChestChunk( chunk_pos );
    // Trust the client that a chest block is here — the server has no terrain
    // view. Empty default entry for a chest first opened before anything was
    // put in it.
    ChestContents &contents = cs.chests[ chest_pos ];
    this->chest_subscribers[ chest_pos ].insert( client_fd );
    this->client_open_chest[ client_fd ] = chest_pos;
    this->sendChestContents( server, client_fd, chest_pos, contents );
}

void ServerLogic::handleChestSetSlot( Server &server, int client_fd, const glm::ivec3 &chest_pos, uint32_t slot_index, const InventorySlot &value ) {
    const auto it = this->client_open_chest.find( client_fd );
    if ( it == this->client_open_chest.end( ) || it->second != chest_pos ) {
        pr_debug( "CHEST_SET_SLOT from unsubscribed client:%d", client_fd );
        return;
    }
    InventorySlot clean = value;
    if ( !item_id_valid( clean.item_id ) || clean.quantity <= 0 ) {
        clean.item_id = LAST_BLOCK_ID;
        clean.quantity = 0;
    } else if ( clean.quantity > item_max_stack( clean.item_id ) ) {
        clean.quantity = item_max_stack( clean.item_id );
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( chest_pos ) / CHUNK_SIZE_F );
    ChestChunkState &cs = this->ensureChestChunk( chunk_pos );
    cs.chests[ chest_pos ].slots[ slot_index ] = clean;
    cs.dirty = true;
    this->scheduleChunkSave( chunk_pos );

    // Broadcast the slot write to the other viewers.
    NetChestSetSlotPayload p;
    p.x = chest_pos.x;
    p.y = chest_pos.y;
    p.z = chest_pos.z;
    p.slot_index = slot_index;
    p.slot_value = clean;
    PacketWriter w;
    net_serialize_chest_set_slot( w, p );
    const auto sit = this->chest_subscribers.find( chest_pos );
    if ( sit != this->chest_subscribers.end( ) ) {
        for ( int fd : sit->second ) {
            if ( fd != client_fd ) {
                server.queue_message( fd, NetMsgType::CHEST_SET_SLOT, client_fd, w.buf( ) );
            }
        }
    }
}

// A block update turned a chest into something else. Send the contents to the
// breaker so it can loot them, drop the entry, and persist.
void ServerLogic::handleChestRemoved( Server &server, int client_fd, const glm::ivec3 &chest_pos ) {
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( chest_pos ) / CHUNK_SIZE_F );
    ChestChunkState &cs = this->ensureChestChunk( chunk_pos );
    const auto it = cs.chests.find( chest_pos );
    if ( it == cs.chests.end( ) ) {
        return;
    }
    this->sendChestContents( server, client_fd, chest_pos, it->second );
    cs.chests.erase( it );
    cs.dirty = true;
    this->scheduleChunkSave( chunk_pos );
    const auto sit = this->chest_subscribers.find( chest_pos );
    if ( sit != this->chest_subscribers.end( ) ) {
        for ( int fd : sit->second ) {
            this->client_open_chest.erase( fd );
        }
        this->chest_subscribers.erase( sit );
    }
}

// ---------------------------------------------------------------------------
// Furnaces: same optimistic per-slot + subscriber model as chests, plus
// authoritative ticking. The server has no terrain view, so furnace blocks
// are learned from FURNACE_OPEN and BLOCK_UPDATE placements; the sidecar
// file supplies pre-existing furnaces when a chunk is first touched.
// ---------------------------------------------------------------------------

FurnaceChunkState &ServerLogic::ensureFurnaceChunk( const glm::ivec3 &chunk_pos ) {
    FurnaceChunkState &fs = this->furnace_chunks[ chunk_pos ];
    if ( !fs.loaded ) {
        fs.loaded = true;
        this->map_storage.load_furnaces( chunk_pos, fs.furnaces );
    }
    return fs;
}

void ServerLogic::persistFurnaceChunk( const glm::ivec3 &chunk_pos ) {
    auto it = this->furnace_chunks.find( chunk_pos );
    if ( it == this->furnace_chunks.end( ) || !it->second.dirty ) {
        return;
    }
    this->map_storage.persist_furnaces( chunk_pos, it->second.furnaces );
    it->second.dirty = false;
    it->second.progress_dirty.clear( );
}

bool ServerLogic::furnaceChunkHasSubscribers( const glm::ivec3 &chunk_pos ) const {
    const auto cit = this->furnace_chunks.find( chunk_pos );
    if ( cit == this->furnace_chunks.end( ) ) {
        return false;
    }
    for ( const auto &pair : cit->second.furnaces ) {
        const auto sit = this->furnace_subscribers.find( pair.first );
        if ( sit != this->furnace_subscribers.end( ) && !sit->second.empty( ) ) {
            return true;
        }
    }
    return false;
}

void ServerLogic::sendFurnaceContents( Server &server, int client_fd, const glm::ivec3 &furnace_pos, const FurnaceContents &contents ) {
    NetFurnaceContentsPayload p;
    p.x = furnace_pos.x;
    p.y = furnace_pos.y;
    p.z = furnace_pos.z;
    p.contents = contents;
    PacketWriter w;
    net_serialize_furnace_contents( w, p );
    server.queue_message( client_fd, NetMsgType::FURNACE_CONTENTS, 0, w.take_buf( ) );
}

void ServerLogic::unsubscribeFurnace( int client_fd ) {
    const auto it = this->client_open_furnace.find( client_fd );
    if ( it == this->client_open_furnace.end( ) ) {
        return;
    }
    const glm::ivec3 furnace_pos = it->second;
    this->client_open_furnace.erase( it );
    const auto sit = this->furnace_subscribers.find( furnace_pos );
    if ( sit != this->furnace_subscribers.end( ) ) {
        sit->second.erase( client_fd );
        if ( sit->second.empty( ) ) {
            this->furnace_subscribers.erase( sit );
        }
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( furnace_pos ) / CHUNK_SIZE_F );
    if ( !this->furnaceChunkHasSubscribers( chunk_pos ) ) {
        const auto cit = this->furnace_chunks.find( chunk_pos );
        if ( cit != this->furnace_chunks.end( ) && !cit->second.dirty ) {
            this->furnace_chunks.erase( cit );
        } else if ( cit != this->furnace_chunks.end( ) ) {
            this->scheduleChunkSave( chunk_pos );
        }
    }
}

void ServerLogic::handleFurnaceOpen( Server &server, int client_fd, const glm::ivec3 &furnace_pos ) {
    // A client can only have one furnace open; swap subscriptions. (A client
    // with a chest open that opens a furnace is handled client-side by
    // closing the chest first.)
    this->unsubscribeFurnace( client_fd );
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( furnace_pos ) / CHUNK_SIZE_F );
    FurnaceChunkState &fs = this->ensureFurnaceChunk( chunk_pos );
    // Trust the client that a furnace block is here — the server has no
    // terrain view. Empty default entry for a furnace first opened before
    // anything was put in it.
    FurnaceContents &contents = fs.furnaces[ furnace_pos ];
    this->furnace_subscribers[ furnace_pos ].insert( client_fd );
    this->client_open_furnace[ client_fd ] = furnace_pos;
    this->sendFurnaceContents( server, client_fd, furnace_pos, contents );
}

void ServerLogic::handleFurnaceSetSlot( Server &server, int client_fd, const glm::ivec3 &furnace_pos, uint32_t slot_index, const InventorySlot &value ) {
    const auto it = this->client_open_furnace.find( client_fd );
    if ( it == this->client_open_furnace.end( ) || it->second != furnace_pos ) {
        pr_debug( "FURNACE_SET_SLOT from unsubscribed client:%d", client_fd );
        return;
    }
    InventorySlot clean = value;
    if ( !item_id_valid( clean.item_id ) || clean.quantity <= 0 ) {
        clean.item_id = LAST_BLOCK_ID;
        clean.quantity = 0;
    } else if ( clean.quantity > item_max_stack( clean.item_id ) ) {
        clean.quantity = item_max_stack( clean.item_id );
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( furnace_pos ) / CHUNK_SIZE_F );
    FurnaceChunkState &fs = this->ensureFurnaceChunk( chunk_pos );
    fs.furnaces[ furnace_pos ].slots[ slot_index ] = clean;
    fs.dirty = true;
    this->scheduleChunkSave( chunk_pos );

    // Broadcast the slot write to the other viewers.
    NetFurnaceSetSlotPayload p;
    p.x = furnace_pos.x;
    p.y = furnace_pos.y;
    p.z = furnace_pos.z;
    p.slot_index = slot_index;
    p.slot_value = clean;
    PacketWriter w;
    net_serialize_furnace_set_slot( w, p );
    const auto sit = this->furnace_subscribers.find( furnace_pos );
    if ( sit != this->furnace_subscribers.end( ) ) {
        for ( int fd : sit->second ) {
            if ( fd != client_fd ) {
                server.queue_message( fd, NetMsgType::FURNACE_SET_SLOT, client_fd, w.buf( ) );
            }
        }
    }
}

// A block update turned a furnace into something else. Send the contents to
// the breaker so it can loot them, drop the entry, and persist.
void ServerLogic::handleFurnaceRemoved( Server &server, int client_fd, const glm::ivec3 &furnace_pos ) {
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( furnace_pos ) / CHUNK_SIZE_F );
    FurnaceChunkState &fs = this->ensureFurnaceChunk( chunk_pos );
    const auto it = fs.furnaces.find( furnace_pos );
    if ( it == fs.furnaces.end( ) ) {
        return;
    }
    this->sendFurnaceContents( server, client_fd, furnace_pos, it->second );
    fs.furnaces.erase( it );
    fs.progress_dirty.erase( furnace_pos );
    fs.dirty = true;
    this->scheduleChunkSave( chunk_pos );
    const auto sit = this->furnace_subscribers.find( furnace_pos );
    if ( sit != this->furnace_subscribers.end( ) ) {
        for ( int fd : sit->second ) {
            this->client_open_furnace.erase( fd );
        }
        this->furnace_subscribers.erase( sit );
    }
}

bool ServerLogic::anyFurnaceActive( ) const {
    for ( const auto &chunk_pair : this->furnace_chunks ) {
        for ( const auto &pair : chunk_pair.second.furnaces ) {
            const FurnaceContents &f = pair.second;
            if ( f.burn_ticks > 0 ) {
                return true;
            }
            const InventorySlot &input = f.slots[ FURNACE_SLOT_INPUT ];
            const bool has_work = input.item_id != LAST_BLOCK_ID && smelt_result_item( input.item_id ) != LAST_BLOCK_ID;
            const InventorySlot &fuel = f.slots[ FURNACE_SLOT_FUEL ];
            if ( has_work && fuel.item_id != LAST_BLOCK_ID && fuel.quantity > 0 && furnace_fuel_ticks( fuel.item_id ) > 0 ) {
                return true;
            }
        }
    }
    return false;
}

void ServerLogic::tickFurnaces( Server &server ) {
    // Broadcast throttle: progress snapshots go out every
    // FURNACE_BROADCAST_INTERVAL ticks rather than per-tick.
    static constexpr int FURNACE_BROADCAST_INTERVAL = 5;
    for ( auto &chunk_pair : this->furnace_chunks ) {
        FurnaceChunkState &fs = chunk_pair.second;
        if ( !fs.loaded ) {
            continue;
        }
        for ( auto &pair : fs.furnaces ) {
            const bool was_lit = pair.second.burn_ticks > 0;
            bool changed = false;
            const bool lit_changed = furnace_tick( pair.second, &changed );
            if ( !changed && !lit_changed ) {
                continue;
            }
            fs.dirty = true;
            fs.progress_dirty.insert( pair.first );
            this->scheduleChunkSave( chunk_pair.first );
            if ( lit_changed ) {
                // Swap FURNACE<->LIT_FURNACE for every client and record the
                // edit so it persists in the chunk diff (record_block also
                // updates the neighboring chunk caches). Rotation/extras
                // come from the last-known block state; a furnace the server
                // has never seen placed falls back to canonical.
                const glm::ivec3 &pos = pair.first;
                BlockState state{ AIR, 0, 0, AIR, 0 };
                const auto sit = fs.states.find( pos );
                if ( sit != fs.states.end( ) ) {
                    state = sit->second;
                } else {
                    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
                    const glm::ivec3 diff = pos - chunk_pos * CHUNK_SIZE_I;
                    const int index = Chunk::get_index_from_coords( diff );
                    const auto cit = this->world_cache.find( chunk_pos );
                    if ( cit != this->world_cache.end( ) ) {
                        const auto bit = cit->second.blocks.find( index );
                        if ( bit != cit->second.blocks.end( ) ) {
                            state = bit->second;
                        }
                    }
                }
                state.id = was_lit ? FURNACE : LIT_FURNACE;
                state.display_id = state.id;
                fs.states[ pos ] = state;
                this->record_block( pos, state );
                NetBlockUpdatePayload bp;
                bp.x = pos.x;
                bp.y = pos.y;
                bp.z = pos.z;
                bp.blockState = state;
                PacketWriter w;
                net_serialize_block_update( w, bp );
                for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
                    if ( server.get_data_if_client_connected( fd ) != NULL ) {
                        server.queue_message( fd, NetMsgType::BLOCK_UPDATE, 0, w.buf( ) );
                    }
                }
            }
        }
    }
    // Throttled progress broadcast to viewers.
    if ( --this->furnace_broadcast_countdown <= 0 ) {
        this->furnace_broadcast_countdown = FURNACE_BROADCAST_INTERVAL;
        for ( auto &chunk_pair : this->furnace_chunks ) {
            FurnaceChunkState &fs = chunk_pair.second;
            for ( const glm::ivec3 &pos : fs.progress_dirty ) {
                const auto fit = fs.furnaces.find( pos );
                const auto sit = this->furnace_subscribers.find( pos );
                if ( fit == fs.furnaces.end( ) || sit == this->furnace_subscribers.end( ) ) {
                    continue;
                }
                for ( int fd : sit->second ) {
                    this->sendFurnaceContents( server, fd, pos, fit->second );
                }
            }
            fs.progress_dirty.clear( );
        }
    }
}

void ServerLogic::persistChunk( const glm::ivec3 &chunk_pos ) {
    auto it = this->world_cache.find( chunk_pos );
    if ( it != this->world_cache.end( ) && !it->second.blocks.empty( ) ) {
        const std::map<int, BlockState> &chunk_cache = it->second.blocks;
        BlockState *blocks = ( BlockState * )malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) );
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
            blocks[ i ] = BLOCK_STATE_LAST_BLOCK_ID;
        }
        for ( auto [ chunk_index, blockState ] : chunk_cache ) {
            blocks[ chunk_index ] = blockState;
        }
        this->map_storage.persist_dirty_blocks( chunk_pos, blocks );
        free( blocks );
    }
    // Chest sidecar is independent of the block cache — a chunk can have
    // chest edits without block edits (pure inventory changes).
    this->persistChestChunk( chunk_pos );
    this->persistFurnaceChunk( chunk_pos );
}

void ServerLogic::persistAll( ) {
    this->map_storage.write_world_time( this->worldTimeNow( ) );
    this->entities.flush_all( );
    size_t saved = 0;
    for ( const auto &[ chunk_pos, entry ] : this->world_cache ) {
        if ( entry.blocks.empty( ) ) {
            continue;
        }
        this->persistChunk( chunk_pos );
        saved++;
    }
    // Chest chunks may exist without a block-cache entry.
    for ( const auto &[ chunk_pos, entry ] : this->chest_chunks ) {
        (void)entry;
        this->persistChestChunk( chunk_pos );
    }
    this->chest_chunks.clear( );
    this->chest_subscribers.clear( );
    this->client_open_chest.clear( );
    // Furnace chunks likewise; progress_dirty is cleared by persist.
    for ( const auto &[ chunk_pos, entry ] : this->furnace_chunks ) {
        (void)entry;
        this->persistFurnaceChunk( chunk_pos );
    }
    this->furnace_chunks.clear( );
    this->furnace_subscribers.clear( );
    this->client_open_furnace.clear( );
    pr_debug( "World saved (%zu chunks)", saved );
    this->world_cache.clear( );
    this->wake_schedule.clear( );
    this->chunk_wake.clear( );
}

void ServerLogic::flushAll( ) {
    this->persistAll( );
}

void ServerLogic::processTimers( Server &server ) {
    auto now = std::chrono::steady_clock::now( );
    while ( !this->wake_schedule.empty( ) && this->wake_schedule.begin( )->first <= now ) {
        auto it = this->wake_schedule.begin( );
        glm::ivec3 chunk_pos = it->second;
        this->wake_schedule.erase( it );
        this->chunk_wake.erase( chunk_pos );
        pr_debug( "Idle-saving chunk:%d %d %d", chunk_pos.x, chunk_pos.y, chunk_pos.z );
        this->persistChunk( chunk_pos );
        this->world_cache.erase( chunk_pos );
        // Chest data survives eviction only while nobody is viewing it;
        // subscribers keep the entry (and its file stays saved) so an open
        // chest remains editable.
        if ( !this->chestChunkHasSubscribers( chunk_pos ) ) {
            this->chest_chunks.erase( chunk_pos );
        }
        if ( !this->furnaceChunkHasSubscribers( chunk_pos ) ) {
            const auto fit = this->furnace_chunks.find( chunk_pos );
            if ( fit != this->furnace_chunks.end( ) && !fit->second.dirty ) {
                this->furnace_chunks.erase( fit );
            }
        }
    }
    // Furnace ticking runs at UPS_RATE, but only while some furnace can
    // still change — an idle cached furnace doesn't earn a wake. When a
    // message wakes the loop anyway, up to 8 catch-up ticks run.
    if ( this->anyFurnaceActive( ) ) {
        const auto tick_interval = std::chrono::microseconds( 1000000 / UPS_RATE );
        int catch_up = 0;
        while ( now >= this->next_furnace_tick && catch_up < 8 ) {
            this->tickFurnaces( server );
            this->next_furnace_tick += tick_interval;
            catch_up++;
        }
        if ( now >= this->next_furnace_tick ) {
            this->next_furnace_tick = now + tick_interval;
        }
    } else {
        this->next_furnace_tick = std::chrono::steady_clock::now( ) + std::chrono::microseconds( 1000000 / UPS_RATE );
    }
    if ( this->world_time_running && now >= this->next_time_broadcast ) {
        this->next_time_broadcast += std::chrono::seconds( WORLD_TIME_SYNC_SECONDS );
        // If we fell a full interval behind (long event loop stall), don't
        // fire a burst of catch-up broadcasts.
        if ( now >= this->next_time_broadcast ) {
            this->next_time_broadcast = now + std::chrono::seconds( WORLD_TIME_SYNC_SECONDS );
        }
        this->broadcastWorldTime( server );
    }
}

int ServerLogic::nextWakeTimeoutMs( ) const {
    auto wake = std::chrono::steady_clock::time_point::max( );
    if ( !this->wake_schedule.empty( ) ) {
        wake = this->wake_schedule.begin( )->first;
    }
    if ( this->world_time_running && this->next_time_broadcast < wake ) {
        wake = this->next_time_broadcast;
    }
    if ( this->anyFurnaceActive( ) && this->next_furnace_tick < wake ) {
        wake = this->next_furnace_tick;
    }
    if ( wake == std::chrono::steady_clock::time_point::max( ) ) {
        return -1;
    }
    auto now = std::chrono::steady_clock::now( );
    auto delta = std::chrono::duration_cast<std::chrono::milliseconds>( wake - now );
    long long ms = delta.count( );
    if ( ms < 0 ) {
        return 0;
    }
    if ( ms > INT_MAX ) {
        return INT_MAX;
    }
    return static_cast<int>( ms );
}

ChunkCacheEntry *ServerLogic::loadIntoCache( const glm::ivec3 &chunk_offset ) {
    // Only record_block reaches here, and it schedules an idle-save timer
    // right after — so every cache entry is persisted and evicted by
    // processTimers once its edits go quiet.
    ChunkCacheEntry &entry = this->world_cache[ chunk_offset ];
    entry.blocks.clear( );
    // This chunk is about to gain an edit — it's no longer known-empty.
    this->known_empty_chunks.erase( chunk_offset );
    this->loadDiffsFromDisk( chunk_offset, entry.blocks );
    return &entry;
}

bool ServerLogic::loadDiffsFromDisk( const glm::ivec3 &chunk_offset, std::map<int, BlockState> &out ) {
    out.clear( );
    // Check file existence before allocating a 350KB buffer. For a fresh
    // world where most chunks have no file, this avoids both a malloc/free
    // and a wasted fread attempt per chunk.
    if ( !this->map_storage.check_if_chunk_exists( chunk_offset ) ) {
        return false;
    }
    BlockState *blocks = ( BlockState * )malloc( CHUNK_BLOCK_SIZE * sizeof( BlockState ) );
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        blocks[ i ] = BLOCK_STATE_LAST_BLOCK_ID;
    }
    int dirty = 0;
    int ret = this->map_storage.load_blocks( chunk_offset, blocks, dirty, true );
    if ( ret == 0 ) {
        free( blocks );
        // File existed according to check_if_chunk_exists but load_blocks
        // returned 0 (race or read error).
        return false;
    }
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; ++i ) {
        BlockState &blockState = blocks[ i ];
        if ( blockState.id != LAST_BLOCK_ID ) {
            out[ i ] = blockState;
        }
    }
    free( blocks );
    return true;
}

void ServerLogic::on_client_message( Server &server, int client_fd, NetMsgType type, int32_t player_id, const std::vector<uint8_t> &payload ) {

    PacketReader r( payload.data( ), payload.size( ) );

    if ( this->entities.on_client_message( server, client_fd, type, payload ) ) {
        return;
    }

    if ( type == NetMsgType::BLOCK_UPDATE ) {
        NetBlockUpdatePayload bu;
        if ( !net_deserialize_block_update( r, bu ) ) {
            pr_debug( "Malformed BLOCK_UPDATE from:%d", client_fd );
            return;
        }
        glm::ivec3 block_pos = glm::ivec3( bu.x, bu.y, bu.z );
        // A chest block being overwritten destroys its container: hand the
        // contents to the breaker for looting.
        if ( !block_is_chest( bu.blockState.id ) ) {
            const glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
            ChestChunkState &cs = this->ensureChestChunk( chunk_pos );
            if ( cs.chests.find( block_pos ) != cs.chests.end( ) ) {
                this->handleChestRemoved( server, client_fd, block_pos );
            }
        }
        // Same for furnaces: learn placements (for lit-swap state and ticking
        // once opened) and hand contents to the breaker on removal.
        {
            const glm::ivec3 chunk_pos = glm::floor( glm::vec3( block_pos ) / CHUNK_SIZE_F );
            FurnaceChunkState &fs = this->ensureFurnaceChunk( chunk_pos );
            if ( block_is_furnace( bu.blockState.id ) ) {
                fs.states[ block_pos ] = bu.blockState;
            } else {
                fs.states.erase( block_pos );
                if ( fs.furnaces.find( block_pos ) != fs.furnaces.end( ) ) {
                    this->handleFurnaceRemoved( server, client_fd, block_pos );
                }
            }
        }
        this->record_block( block_pos, bu.blockState );

        // Forward to all other clients.
        PacketWriter w;
        net_serialize_block_update( w, bu );
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                continue;
            }
            if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
                server.queue_message( prop_id, NetMsgType::BLOCK_UPDATE, client_fd, w.buf( ) );
            }
        }
    } else if ( type == NetMsgType::CHUNK_DIFF_REQUEST ) {
        NetChunkDiffRequestPayload req;
        if ( !net_deserialize_chunk_diff_request( r, req ) ) {
            pr_debug( "Malformed CHUNK_DIFF_REQUEST from:%d", client_fd );
            return;
        }
        // Validate box dimensions.
        if ( req.size_x > NET_MAX_BOX_DIM || req.size_y > NET_MAX_BOX_DIM || req.size_z > NET_MAX_BOX_DIM ) {
            pr_debug( "CHUNK_DIFF_REQUEST box too large: %u x %u x %u from:%d", req.size_x, req.size_y, req.size_z, client_fd );
            return;
        }
        // Respond for each chunk in the box.
        for ( uint8_t dx = 0; dx < req.size_x; dx++ ) {
            for ( uint8_t dy = 0; dy < req.size_y; dy++ ) {
                for ( uint8_t dz = 0; dz < req.size_z; dz++ ) {
                    glm::ivec3 chunk_pos = glm::ivec3( req.min_x + dx, req.min_y + dy, req.min_z + dz );
                    this->respondToChunkRequest( server, client_fd, chunk_pos );
                }
            }
        }
    } else if ( type == NetMsgType::PLAYER_LOCATION ) {
        NetPlayerPayload p;
        if ( !net_deserialize_player( r, p ) ) {
            pr_debug( "Malformed PLAYER_LOCATION from:%d", client_fd );
            return;
        }
        this->entities.on_player_position( server, client_fd, p.x, p.y, p.z );
        // Forward to all other clients.
        PacketWriter w;
        net_serialize_player( w, p );
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                continue;
            }
            if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
                server.queue_message( prop_id, NetMsgType::PLAYER_LOCATION, client_fd, w.buf( ) );
            }
        }
    } else if ( type == NetMsgType::CHEST_OPEN ) {
        NetChestPosPayload p;
        if ( !net_deserialize_chest_pos( r, p ) ) {
            pr_debug( "Malformed CHEST_OPEN from:%d", client_fd );
            return;
        }
        this->handleChestOpen( server, client_fd, glm::ivec3( p.x, p.y, p.z ) );
    } else if ( type == NetMsgType::CHEST_CLOSE ) {
        NetChestPosPayload p;
        if ( !net_deserialize_chest_pos( r, p ) ) {
            pr_debug( "Malformed CHEST_CLOSE from:%d", client_fd );
            return;
        }
        this->unsubscribeChest( client_fd );
    } else if ( type == NetMsgType::CHEST_SET_SLOT ) {
        NetChestSetSlotPayload p;
        if ( !net_deserialize_chest_set_slot( r, p ) ) {
            pr_debug( "Malformed CHEST_SET_SLOT from:%d", client_fd );
            return;
        }
        this->handleChestSetSlot( server, client_fd, glm::ivec3( p.x, p.y, p.z ), p.slot_index, p.slot_value );
    } else if ( type == NetMsgType::FURNACE_OPEN ) {
        NetChestPosPayload p;
        if ( !net_deserialize_chest_pos( r, p ) ) {
            pr_debug( "Malformed FURNACE_OPEN from:%d", client_fd );
            return;
        }
        this->handleFurnaceOpen( server, client_fd, glm::ivec3( p.x, p.y, p.z ) );
    } else if ( type == NetMsgType::FURNACE_CLOSE ) {
        NetChestPosPayload p;
        if ( !net_deserialize_chest_pos( r, p ) ) {
            pr_debug( "Malformed FURNACE_CLOSE from:%d", client_fd );
            return;
        }
        this->unsubscribeFurnace( client_fd );
    } else if ( type == NetMsgType::FURNACE_SET_SLOT ) {
        NetFurnaceSetSlotPayload p;
        if ( !net_deserialize_furnace_set_slot( r, p ) ) {
            pr_debug( "Malformed FURNACE_SET_SLOT from:%d", client_fd );
            return;
        }
        this->handleFurnaceSetSlot( server, client_fd, glm::ivec3( p.x, p.y, p.z ), p.slot_index, p.slot_value );
    } else if ( type == NetMsgType::WORLD_TIME ) {
        NetWorldTimePayload p;
        if ( !net_deserialize_world_time( r, p ) ) {
            pr_debug( "Malformed WORLD_TIME from:%d", client_fd );
            return;
        }
        // A client set the time (e.g. PageUp/PgDn): adopt it as the new
        // clock base and forward to the other clients so everyone snaps to
        // the same value.
        this->world_time_base = p.world_time;
        this->world_time_anchor = std::chrono::steady_clock::now( );
        PacketWriter w;
        net_serialize_world_time( w, p );
        for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
            if ( prop_id == client_fd ) {
                continue;
            }
            if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
                server.queue_message( prop_id, NetMsgType::WORLD_TIME, client_fd, w.buf( ) );
            }
        }
    }
}

void ServerLogic::on_client_disconnected( Server &server, int client_fd ) {
    this->unsubscribeChest( client_fd );
    this->unsubscribeFurnace( client_fd );
    this->entities.on_client_disconnected( server, client_fd );
    for ( int prop_id = 0; prop_id < MAX_CLIENT_FDS; prop_id++ ) {
        if ( prop_id == client_fd ) {
            continue;
        }
        if ( server.get_data_if_client_connected( prop_id ) != NULL ) {
            server.queue_empty( prop_id, NetMsgType::PLAYER_DISCONNECTED, client_fd );
            pr_debug( "Notifying existing player:%d of removed player:%d", prop_id, client_fd );
        }
    }
    // If this was the last client, save everything to disk and free memory.
    // Otherwise, let the per-chunk idle timers handle persistence.
    bool any_connected = false;
    for ( int id = 0; id < MAX_CLIENT_FDS; id++ ) {
        if ( id == client_fd ) {
            continue;
        }
        if ( server.get_data_if_client_connected( id ) != NULL ) {
            any_connected = true;
            break;
        }
    }
    if ( !any_connected ) {
        // Freeze the world clock where it is; flushAll persists it.
        this->world_time_base = this->worldTimeNow( );
        this->world_time_running = false;
        pr_debug( "Last client disconnected; flushing all chunks to disk" );
        this->flushAll( );
    }
}
