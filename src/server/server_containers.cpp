// Server container sync — see server_containers.hpp for the model. This is
// the generic merge of what used to be ~500 lines of near-verbatim
// chest/furnace machinery in server_logic.cpp: the differences that remain
// are the NetContainerKindDef rows (wire arity, block predicate, sidecar
// file, ticking/lit-swap hooks) in common/container.cpp.

#include "server/server_containers.hpp"

#include <stdio.h>

#include "common/chunk.hpp"
#include "common/constants.hpp"
#include "server/server.hpp"
#include "server/server_logic.hpp"

#define pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

void ServerContainers::init( ServerLogic *logic ) {
    this->logic = logic;
    for ( int i = 0; i < NET_CONTAINER_KIND_COUNT; i++ ) {
        this->sets[ i ].def = net_container_kind_def( static_cast<uint8_t>( i ) );
    }
    this->next_tick = std::chrono::steady_clock::now( );
}

ServerContainerChunk &ServerContainers::ensureChunk( Set &set, const glm::ivec3 &chunk_pos ) {
    ServerContainerChunk &chunk = set.chunks[ chunk_pos ];
    if ( !chunk.loaded ) {
        chunk.loaded = true;
        // First access in a chunk pulls in every container the sidecar file
        // has, so persisting writes the complete set.
        ContainerContentsMap loaded;
        set.def->load( this->logic->map_storage, chunk_pos, loaded );
        for ( auto &pair : loaded ) {
            chunk.entries[ pair.first ].contents = std::move( pair.second );
        }
    }
    return chunk;
}

ServerContainerEntry &ServerContainers::entryFor( Set &set, ServerContainerChunk &chunk, const glm::ivec3 &pos ) {
    ServerContainerEntry &entry = chunk.entries[ pos ];
    // Entries always carry the kind's exact slot/aux arity — a default-
    // constructed entry gets normalized here (loads already arrive exact).
    if ( static_cast<int>( entry.contents.slots.size( ) ) != set.def->slots ) {
        entry.contents.slots.assign( set.def->slots, InventorySlot{ LAST_BLOCK_ID, 0 } );
    }
    if ( static_cast<int>( entry.contents.aux.size( ) ) != set.def->aux ) {
        entry.contents.aux.assign( set.def->aux, 0 );
    }
    return entry;
}

void ServerContainers::persistChunk( Set &set, const glm::ivec3 &chunk_pos ) {
    const auto it = set.chunks.find( chunk_pos );
    if ( it == set.chunks.end( ) || !it->second.dirty ) {
        return;
    }
    ContainerContentsMap contents;
    for ( const auto &pair : it->second.entries ) {
        contents[ pair.first ] = pair.second.contents;
    }
    set.def->persist( this->logic->map_storage, chunk_pos, contents );
    it->second.dirty = false;
    it->second.progress_dirty.clear( );
}

bool ServerContainers::chunkHasSubscribers( const Set &set, const glm::ivec3 &chunk_pos ) const {
    const auto cit = set.chunks.find( chunk_pos );
    if ( cit == set.chunks.end( ) ) {
        return false;
    }
    for ( const auto &pair : cit->second.entries ) {
        const auto sit = set.subscribers.find( pair.first );
        if ( sit != set.subscribers.end( ) && !sit->second.empty( ) ) {
            return true;
        }
    }
    return false;
}

void ServerContainers::sendContents( Server &server, int client_fd, const Set &set, const glm::ivec3 &pos, const ServerContainerEntry &entry ) {
    NetContainerContentsPayload p;
    p.kind = set.def->kind;
    p.x = pos.x;
    p.y = pos.y;
    p.z = pos.z;
    p.revision = entry.revision;
    p.slots = entry.contents.slots;
    p.aux = entry.contents.aux;
    server.send( client_fd, NetMsgType::CONTAINER_CONTENTS, p );
}

void ServerContainers::sendCorrection( Server &server, int client_fd, const Set &set, const glm::ivec3 &pos ) {
    // Never materialize an entry for the correction — an unknown container
    // corrects to an empty snapshot.
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    ServerContainerEntry empty;
    empty.contents.slots.assign( set.def->slots, InventorySlot{ LAST_BLOCK_ID, 0 } );
    empty.contents.aux.assign( set.def->aux, 0 );
    const ServerContainerEntry *entry = &empty;
    const auto cit = set.chunks.find( chunk_pos );
    if ( cit != set.chunks.end( ) ) {
        const auto eit = cit->second.entries.find( pos );
        if ( eit != cit->second.entries.end( ) ) {
            entry = &eit->second;
        }
    }
    this->sendContents( server, client_fd, set, pos, *entry );
}

void ServerContainers::unsubscribe( Set &set, int client_fd ) {
    const auto it = set.client_open.find( client_fd );
    if ( it == set.client_open.end( ) ) {
        return;
    }
    const glm::ivec3 pos = it->second;
    set.client_open.erase( it );
    const auto sit = set.subscribers.find( pos );
    if ( sit != set.subscribers.end( ) ) {
        sit->second.erase( client_fd );
        if ( sit->second.empty( ) ) {
            set.subscribers.erase( sit );
        }
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    if ( !this->chunkHasSubscribers( set, chunk_pos ) ) {
        // Last viewer closed: evict an unmodified cache now, or let the
        // idle timer write+evict a dirty one.
        const auto cit = set.chunks.find( chunk_pos );
        if ( cit != set.chunks.end( ) && !cit->second.dirty ) {
            set.chunks.erase( cit );
        } else if ( cit != set.chunks.end( ) ) {
            this->logic->scheduleChunkSave( chunk_pos );
        }
    }
}

void ServerContainers::handleOpen( Server &server, Set &set, int client_fd, const glm::ivec3 &pos ) {
    // A client holds at most one open container — drop any other
    // subscription (all kinds; the client's CLOSE is best-effort).
    for ( Set &other : this->sets ) {
        this->unsubscribe( other, client_fd );
    }
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    ServerContainerChunk &chunk = this->ensureChunk( set, chunk_pos );
    // Trust the client that a container block is here — the server has no
    // terrain view. Empty default entry for a container first opened before
    // anything was put in it.
    ServerContainerEntry &entry = this->entryFor( set, chunk, pos );
    set.subscribers[ pos ].insert( client_fd );
    set.client_open[ client_fd ] = pos;
    this->sendContents( server, client_fd, set, pos, entry );
}

void ServerContainers::handleSetSlot( Server &server, Set &set, int client_fd, const glm::ivec3 &pos, uint32_t slot_index, const InventorySlot &value ) {
    const auto it = set.client_open.find( client_fd );
    if ( it == set.client_open.end( ) || it->second != pos ) {
        // Unauthorized write — the optimistic client already applied it, so
        // push the authoritative snapshot back; the client self-heals.
        pr_debug( "CONTAINER_SET_SLOT %s from unsubscribed client:%d — sending correction", set.def->name, client_fd );
        this->sendCorrection( server, client_fd, set, pos );
        return;
    }
    const InventorySlot clean = container_clean_slot( value );
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    ServerContainerChunk &chunk = this->ensureChunk( set, chunk_pos );
    ServerContainerEntry &entry = this->entryFor( set, chunk, pos );
    entry.contents.slots[ slot_index ] = clean;
    entry.revision++;
    chunk.dirty = true;
    this->logic->scheduleChunkSave( chunk_pos );

    // Broadcast the accepted write, stamped with its new revision, to the
    // other viewers.
    NetContainerSetSlotPayload p;
    p.kind = set.def->kind;
    p.x = pos.x;
    p.y = pos.y;
    p.z = pos.z;
    p.slot_index = slot_index;
    p.slot_value = clean;
    p.revision = entry.revision;
    PacketWriter w;
    net_serialize_container_set_slot( w, p );
    const auto sit = set.subscribers.find( pos );
    if ( sit != set.subscribers.end( ) ) {
        server.broadcast_to( sit->second, NetMsgType::CONTAINER_SET_SLOT, client_fd, w.buf( ), client_fd );
    }
}

// A block update turned a container into something else. Send the contents
// to the breaker so it can loot them, drop the entry, and persist.
void ServerContainers::handleRemoved( Server &server, Set &set, int client_fd, const glm::ivec3 &pos ) {
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    ServerContainerChunk &chunk = this->ensureChunk( set, chunk_pos );
    const auto it = chunk.entries.find( pos );
    if ( it == chunk.entries.end( ) ) {
        return;
    }
    this->sendContents( server, client_fd, set, pos, it->second );
    chunk.entries.erase( it );
    chunk.states.erase( pos );
    chunk.progress_dirty.erase( pos );
    chunk.dirty = true;
    this->logic->scheduleChunkSave( chunk_pos );
    const auto sit = set.subscribers.find( pos );
    if ( sit != set.subscribers.end( ) ) {
        for ( int fd : sit->second ) {
            set.client_open.erase( fd );
        }
        set.subscribers.erase( sit );
    }
}

bool ServerContainers::on_client_message( Server &server, int client_fd, NetMsgType type, PacketReader &r ) {
    switch ( type ) {
        case NetMsgType::CONTAINER_OPEN: {
            NetContainerPosPayload p;
            if ( !net_deserialize_container_pos( r, p ) ) {
                pr_debug( "Malformed CONTAINER_OPEN from:%d", client_fd );
                return true;
            }
            this->handleOpen( server, this->sets[ static_cast<uint8_t>( p.kind ) ], client_fd, glm::ivec3( p.x, p.y, p.z ) );
            return true;
        }
        case NetMsgType::CONTAINER_CLOSE: {
            NetContainerPosPayload p;
            if ( !net_deserialize_container_pos( r, p ) ) {
                pr_debug( "Malformed CONTAINER_CLOSE from:%d", client_fd );
                return true;
            }
            Set &set = this->sets[ static_cast<uint8_t>( p.kind ) ];
            // CLOSE is scoped to the container it names — a stale close for
            // a different position mustn't drop the current subscription.
            const auto it = set.client_open.find( client_fd );
            if ( it != set.client_open.end( ) && it->second == glm::ivec3( p.x, p.y, p.z ) ) {
                this->unsubscribe( set, client_fd );
            }
            return true;
        }
        case NetMsgType::CONTAINER_SET_SLOT: {
            NetContainerSetSlotPayload p;
            if ( !net_deserialize_container_set_slot( r, p ) ) {
                pr_debug( "Malformed CONTAINER_SET_SLOT from:%d", client_fd );
                return true;
            }
            this->handleSetSlot( server, this->sets[ static_cast<uint8_t>( p.kind ) ], client_fd,
                                 glm::ivec3( p.x, p.y, p.z ), p.slot_index, p.slot_value );
            return true;
        }
        default:
            return false;
    }
}

void ServerContainers::on_block_update( Server &server, int client_fd, const glm::ivec3 &pos, const BlockState &state ) {
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    for ( Set &set : this->sets ) {
        ServerContainerChunk &chunk = this->ensureChunk( set, chunk_pos );
        if ( set.def->block_is( state.id ) ) {
            // Placement: lit-swap kinds keep the last-seen block state so a
            // future swap preserves rotation/extras.
            if ( set.def->lit != nullptr ) {
                chunk.states[ pos ] = state;
            }
        } else {
            chunk.states.erase( pos );
            if ( chunk.entries.find( pos ) != chunk.entries.end( ) ) {
                this->handleRemoved( server, set, client_fd, pos );
            }
        }
    }
}

void ServerContainers::on_client_disconnected( int client_fd ) {
    for ( Set &set : this->sets ) {
        this->unsubscribe( set, client_fd );
    }
}

bool ServerContainers::any_active( ) const {
    for ( const Set &set : this->sets ) {
        if ( set.def->active == nullptr ) {
            continue;
        }
        for ( const auto &chunk_pair : set.chunks ) {
            for ( const auto &pair : chunk_pair.second.entries ) {
                if ( set.def->active( pair.second.contents ) ) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ServerContainers::next_wake( std::chrono::steady_clock::time_point &wake ) const {
    if ( this->any_active( ) && this->next_tick < wake ) {
        wake = this->next_tick;
    }
}

// Advance every cached container of a ticking kind one tick: contents
// mutations mark the chunk dirty + progress_dirty (throttled snapshot
// broadcast below), and a lit edge swaps the kind's unlit/lit block for
// every client while recording the edit into the chunk diff.
void ServerContainers::tickSet( Server &server, Set &set ) {
    static constexpr int BROADCAST_INTERVAL = 5;
    for ( auto &chunk_pair : set.chunks ) {
        const glm::ivec3 &chunk_pos = chunk_pair.first;
        ServerContainerChunk &chunk = chunk_pair.second;
        if ( !chunk.loaded ) {
            continue;
        }
        for ( auto &pair : chunk.entries ) {
            bool lit_changed = false;
            bool changed = false;
            set.def->tick( pair.second.contents, &lit_changed, &changed );
            if ( !changed && !lit_changed ) {
                continue;
            }
            pair.second.revision++;
            chunk.dirty = true;
            chunk.progress_dirty.insert( pair.first );
            this->logic->scheduleChunkSave( chunk_pos );
            if ( lit_changed ) {
                const glm::ivec3 &pos = pair.first;
                BlockState state{ AIR, 0, 0, AIR, 0 };
                const auto sit = chunk.states.find( pos );
                if ( sit != chunk.states.end( ) ) {
                    state = sit->second;
                } else {
                    const glm::ivec3 diff = pos - chunk_pos * CHUNK_SIZE_I;
                    const int index = Chunk::get_index_from_coords( diff );
                    const auto cit = this->logic->world_cache.find( chunk_pos );
                    if ( cit != this->logic->world_cache.end( ) ) {
                        const auto bit = cit->second.blocks.find( index );
                        if ( bit != cit->second.blocks.end( ) ) {
                            state = bit->second;
                        }
                    }
                }
                state.id = set.def->lit( pair.second.contents ) ? set.def->lit_block : set.def->unlit_block;
                state.display_id = state.id;
                chunk.states[ pos ] = state;
                this->logic->record_block( pos, state );
                NetBlockUpdatePayload bp;
                bp.x = pos.x;
                bp.y = pos.y;
                bp.z = pos.z;
                bp.blockState = state;
                PacketWriter w;
                net_serialize_block_update( w, bp );
                server.broadcast( NetMsgType::BLOCK_UPDATE, 0, w.buf( ) );
            }
        }
    }
    // Throttled progress broadcast to viewers.
    if ( --set.broadcast_countdown <= 0 ) {
        set.broadcast_countdown = BROADCAST_INTERVAL;
        for ( auto &chunk_pair : set.chunks ) {
            ServerContainerChunk &chunk = chunk_pair.second;
            for ( const glm::ivec3 &pos : chunk.progress_dirty ) {
                const auto eit = chunk.entries.find( pos );
                const auto sit = set.subscribers.find( pos );
                if ( eit == chunk.entries.end( ) || sit == set.subscribers.end( ) ) {
                    continue;
                }
                NetContainerContentsPayload p;
                p.kind = set.def->kind;
                p.x = pos.x;
                p.y = pos.y;
                p.z = pos.z;
                p.revision = eit->second.revision;
                p.slots = eit->second.contents.slots;
                p.aux = eit->second.contents.aux;
                PacketWriter w;
                net_serialize_container_contents( w, p );
                server.broadcast_to( sit->second, NetMsgType::CONTAINER_CONTENTS, 0, w.buf( ) );
            }
            chunk.progress_dirty.clear( );
        }
    }
}

void ServerContainers::tick_due( Server &server, std::chrono::steady_clock::time_point now ) {
    const auto tick_interval = std::chrono::microseconds( 1000000 / UPS_RATE );
    if ( this->any_active( ) ) {
        int catch_up = 0;
        while ( now >= this->next_tick && catch_up < 8 ) {
            for ( Set &set : this->sets ) {
                if ( set.def->tick != nullptr ) {
                    this->tickSet( server, set );
                }
            }
            this->next_tick += tick_interval;
            catch_up++;
        }
        if ( now >= this->next_tick ) {
            this->next_tick = now + tick_interval;
        }
    } else {
        this->next_tick = now + tick_interval;
    }
}

void ServerContainers::persist_chunk( const glm::ivec3 &chunk_pos ) {
    for ( Set &set : this->sets ) {
        this->persistChunk( set, chunk_pos );
    }
}

void ServerContainers::evict_unwatched( const glm::ivec3 &chunk_pos ) {
    // Container data survives eviction only while nobody is viewing it;
    // subscribers keep the entry (and its file stays saved) so an open
    // container remains editable.
    for ( Set &set : this->sets ) {
        if ( !this->chunkHasSubscribers( set, chunk_pos ) ) {
            set.chunks.erase( chunk_pos );
        }
    }
}

void ServerContainers::persist_all( ) {
    for ( Set &set : this->sets ) {
        for ( const auto &[ chunk_pos, entry ] : set.chunks ) {
            (void)entry;
            this->persistChunk( set, chunk_pos );
        }
        set.chunks.clear( );
        set.subscribers.clear( );
        set.client_open.clear( );
    }
}

const ServerContainerEntry *ServerContainers::find( NetContainerKind kind, const glm::ivec3 &pos ) const {
    const uint8_t k = static_cast<uint8_t>( kind );
    if ( k >= NET_CONTAINER_KIND_COUNT ) {
        return nullptr;
    }
    const Set &set = this->sets[ k ];
    const glm::ivec3 chunk_pos = glm::floor( glm::vec3( pos ) / CHUNK_SIZE_F );
    const auto cit = set.chunks.find( chunk_pos );
    if ( cit == set.chunks.end( ) ) {
        return nullptr;
    }
    const auto eit = cit->second.entries.find( pos );
    return eit == cit->second.entries.end( ) ? nullptr : &eit->second;
}

int ServerContainers::subscriber_count( NetContainerKind kind, const glm::ivec3 &pos ) const {
    const uint8_t k = static_cast<uint8_t>( kind );
    if ( k >= NET_CONTAINER_KIND_COUNT ) {
        return 0;
    }
    const auto sit = this->sets[ k ].subscribers.find( pos );
    return sit == this->sets[ k ].subscribers.end( ) ? 0 : static_cast<int>( sit->second.size( ) );
}
