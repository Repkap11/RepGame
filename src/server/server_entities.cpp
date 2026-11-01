// ServerEntities: per-chunk entity ownership, disk persistence and relay
// arbitration. See server_entities.hpp for the model.

#include "server/server_entities.hpp"
#include "server/server.hpp"
#include "common/constants.hpp"
#include "common/item.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <stdio.h>

#define se_pr_debug( fmt, ... ) fprintf( stdout, "%s:%d:%s():" fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__ );

// Chunk index of a world position (all three chunk dims are CHUNK_SIZE_*).
static glm::ivec3 chunk_of_pos( double x, double y, double z ) {
    return glm::ivec3( static_cast<int>( std::floor( x / CHUNK_SIZE_X ) ),
                       static_cast<int>( std::floor( y / CHUNK_SIZE_Y ) ),
                       static_cast<int>( std::floor( z / CHUNK_SIZE_Z ) ) );
}

static int chebyshev( const glm::ivec3 &a, const glm::ivec3 &b ) {
    return std::max( std::max( std::abs( a.x - b.x ), std::abs( a.y - b.y ) ), std::abs( a.z - b.z ) );
}

void ServerEntities::init( MapStorage *storage ) {
    this->storage = storage;
}

NetEntityId ServerEntities::mint_server_id( ) {
    const uint32_t counter = this->next_server_entity++;
    if ( this->next_server_entity == 0 ) {
        this->next_server_entity = 1;
    }
    return static_cast<NetEntityId>( counter ); // session 0
}

bool ServerEntities::view_covers( const EntityClientView &v, const glm::ivec3 &chunk ) const {
    if ( !v.valid ) {
        return false;
    }
    return std::abs( chunk.x - v.center.x ) <= v.rx && //
           std::abs( chunk.z - v.center.z ) <= v.rx && //
           std::abs( chunk.y - v.center.y ) <= v.ry;
}

int ServerEntities::distance_to( int fd, const glm::ivec3 &chunk ) const {
    const auto it = this->clients.find( fd );
    if ( it == this->clients.end( ) ) {
        return INT_MAX;
    }
    // Distance from the client's player chunk; until the first
    // PLAYER_LOCATION arrives the view center stands in for it.
    const glm::ivec3 &from = it->second.has_player_chunk ? it->second.player_chunk : it->second.center;
    return chebyshev( from, chunk );
}

uint8_t ServerEntities::role_of( int fd, const glm::ivec3 &chunk ) const {
    const auto it = this->chunks.find( chunk );
    if ( it == this->chunks.end( ) ) {
        return NET_ROLE_NONE;
    }
    if ( it->second.owner_fd == fd ) {
        return NET_ROLE_OWNER;
    }
    if ( it->second.viewers.count( fd ) ) {
        return NET_ROLE_VIEWER;
    }
    return NET_ROLE_NONE;
}

void ServerEntities::send_chunk_role( Server &server, int fd, const glm::ivec3 &chunk, uint8_t role ) {
    NetChunkRolePayload p;
    p.cx = chunk.x;
    p.cy = chunk.y;
    p.cz = chunk.z;
    p.role = role;
    p.populated = 0;
    if ( role != NET_ROLE_NONE ) {
        const auto it = this->chunks.find( chunk );
        if ( it != this->chunks.end( ) ) {
            p.populated = it->second.populated ? 1 : 0;
            p.records.reserve( it->second.entities.size( ) );
            for ( const auto &pair : it->second.entities ) {
                p.records.push_back( pair.second );
            }
        }
    }
    PacketWriter w;
    net_serialize_chunk_role( w, p );
    server.queue_message( fd, NetMsgType::CHUNK_ROLE, 0, w.take_buf( ) );
    this->client_roles[ fd ][ chunk ] = role;
    if ( role == NET_ROLE_NONE ) {
        this->client_roles[ fd ].erase( chunk );
    }
}

void ServerEntities::apply_role_delta( Server &server, int fd, const glm::ivec3 &chunk ) {
    const uint8_t now = this->role_of( fd, chunk );
    auto &roles = this->client_roles[ fd ];
    const auto it = roles.find( chunk );
    const uint8_t was = it == roles.end( ) ? NET_ROLE_NONE : it->second;
    if ( now != was ) {
        this->send_chunk_role( server, fd, chunk, now );
    }
}

int ServerEntities::pick_owner( const ServerChunkEntities &ce, const glm::ivec3 &chunk ) const {
    int best_fd = -1;
    int best_dist = INT_MAX;
    for ( int fd : ce.viewers ) {
        const int d = this->distance_to( fd, chunk );
        if ( d < best_dist || ( d == best_dist && ( best_fd < 0 || fd < best_fd ) ) ) {
            best_dist = d;
            best_fd = fd;
        }
    }
    if ( best_fd < 0 ) {
        return -1;
    }
    // Hysteresis: the current owner keeps the chunk while it still covers it
    // and is no more than 2 chunks farther than the best candidate.
    if ( ce.owner_fd >= 0 && ce.viewers.count( ce.owner_fd ) ) {
        if ( this->distance_to( ce.owner_fd, chunk ) <= best_dist + 2 ) {
            return ce.owner_fd;
        }
    }
    return best_fd;
}

void ServerEntities::load_chunk_entities( ServerChunkEntities &ce, const glm::ivec3 &chunk ) {
    if ( this->storage == nullptr ) {
        return;
    }
    std::vector<ItemFileRecord> items;
    this->storage->load_items( chunk, items );
    for ( const ItemFileRecord &rec : items ) {
        NetEntityRecord e;
        e.id = this->mint_server_id( );
        e.kind = NetEntityKind::ITEM;
        e.x = rec.x;
        e.y = rec.y;
        e.z = rec.z;
        e.vx = rec.vx;
        e.vy = rec.vy;
        e.vz = rec.vz;
        e.type = rec.item;
        e.count = rec.count;
        e.age_ticks = rec.age_ticks;
        e.pickup_delay = 0;
        ce.entities[ e.id ] = e;
    }
    std::vector<MobFileRecord> mobs;
    if ( this->storage->load_mobs( chunk, mobs ) ) {
        // File existed (even empty): the populated marker — no natural
        // respawn for this chunk.
        ce.populated = true;
    }
    for ( const MobFileRecord &rec : mobs ) {
        NetEntityRecord e;
        e.id = this->mint_server_id( );
        e.kind = NetEntityKind::MOB;
        e.x = rec.x;
        e.y = rec.y;
        e.z = rec.z;
        e.yaw = rec.yaw;
        e.type = rec.type;
        e.count = rec.variant;
        e.health = rec.health;
        e.flags = rec.flags;
        ce.entities[ e.id ] = e;
    }
}

void ServerEntities::mark_dirty( ServerChunkEntities &ce, NetEntityKind kind ) {
    if ( kind == NetEntityKind::ITEM ) {
        ce.items_dirty = true;
    } else if ( kind == NetEntityKind::MOB ) {
        ce.mobs_dirty = true;
    }
    // BODY records are never persisted.
}

void ServerEntities::persist_chunk_entities( ServerChunkEntities &ce, const glm::ivec3 &chunk ) {
    if ( this->storage == nullptr ) {
        ce.items_dirty = false;
        ce.mobs_dirty = false;
        return;
    }
    if ( ce.items_dirty ) {
        std::vector<ItemFileRecord> out;
        for ( const auto &pair : ce.entities ) {
            const NetEntityRecord &e = pair.second;
            if ( e.kind != NetEntityKind::ITEM || e.count <= 0 ) {
                continue;
            }
            ItemFileRecord rec = {};
            rec.item = e.type;
            rec.count = e.count;
            rec.x = e.x;
            rec.y = e.y;
            rec.z = e.z;
            rec.vx = e.vx;
            rec.vy = e.vy;
            rec.vz = e.vz;
            rec.age_ticks = e.age_ticks;
            out.push_back( rec );
        }
        this->storage->persist_items( chunk, out );
        ce.items_dirty = false;
    }
    if ( ce.mobs_dirty ) {
        // Phase 1: persist every MOB record. Keep the populated marker
        // semantics — only write when a mobs file existed or mobs are
        // present, so a never-populated chunk doesn't gain a marker.
        bool any_mobs = false;
        std::vector<MobFileRecord> out;
        for ( const auto &pair : ce.entities ) {
            const NetEntityRecord &e = pair.second;
            if ( e.kind != NetEntityKind::MOB ) {
                continue;
            }
            any_mobs = true;
            MobFileRecord rec = {};
            rec.type = static_cast<uint16_t>( e.type );
            rec.variant = static_cast<uint16_t>( e.count );
            rec.x = e.x;
            rec.y = e.y;
            rec.z = e.z;
            rec.yaw = e.yaw;
            rec.health = e.health;
            rec.flags = e.flags;
            out.push_back( rec );
        }
        if ( any_mobs || ce.populated ) {
            this->storage->persist_mobs( chunk, out );
        }
        ce.mobs_dirty = false;
    }
}

void ServerEntities::uncover_chunk( const glm::ivec3 &chunk ) {
    const auto it = this->chunks.find( chunk );
    if ( it == this->chunks.end( ) ) {
        return;
    }
    for ( const auto &pair : it->second.entities ) {
        this->entity_chunk.erase( pair.first );
    }
    this->persist_chunk_entities( it->second, chunk );
    for ( int fd : it->second.viewers ) {
        const auto rit = this->client_roles.find( fd );
        if ( rit != this->client_roles.end( ) ) {
            rit->second.erase( chunk );
        }
    }
    this->chunks.erase( it );
}

void ServerEntities::persist_stray_entity( const NetEntityRecord &rec, const glm::ivec3 &chunk ) {
    this->entity_chunk.erase( rec.id );
    if ( this->storage == nullptr || rec.kind == NetEntityKind::BODY ) {
        return;
    }
    // The entity's chunk isn't covered: load its sidecars, append, write
    // back, evict — the record never enters the live table.
    ServerChunkEntities tmp;
    this->load_chunk_entities( tmp, chunk );
    tmp.entities[ rec.id ] = rec;
    tmp.items_dirty = true;
    tmp.mobs_dirty = true;
    this->persist_chunk_entities( tmp, chunk );
}

// Re-evaluate one chunk after a viewer set change: refresh ownership and
// push role deltas to everyone affected (the changed client plus every
// current/former viewer).
void ServerEntities::recompute_boxes( Server &server, int fd, const EntityClientView &old_view, const EntityClientView &new_view ) {
    std::set<glm::ivec3, Compare_I_Vec3> affected;
    const glm::ivec3 omin = old_view.valid ? old_view.center - glm::ivec3( old_view.rx, old_view.ry, old_view.rx ) : glm::ivec3( 0 );
    const glm::ivec3 omax = old_view.valid ? old_view.center + glm::ivec3( old_view.rx, old_view.ry, old_view.rx ) : glm::ivec3( -1 );
    const glm::ivec3 nmin = new_view.valid ? new_view.center - glm::ivec3( new_view.rx, new_view.ry, new_view.rx ) : glm::ivec3( 0 );
    const glm::ivec3 nmax = new_view.valid ? new_view.center + glm::ivec3( new_view.rx, new_view.ry, new_view.rx ) : glm::ivec3( -1 );
    for ( int x = std::min( omin.x, nmin.x ); x <= std::max( omax.x, nmax.x ); x++ ) {
        for ( int y = std::min( omin.y, nmin.y ); y <= std::max( omax.y, nmax.y ); y++ ) {
            for ( int z = std::min( omin.z, nmin.z ); z <= std::max( omax.z, nmax.z ); z++ ) {
                affected.insert( glm::ivec3( x, y, z ) );
            }
        }
    }

    for ( const glm::ivec3 &chunk : affected ) {
        const bool was = this->view_covers( old_view, chunk );
        const bool now = this->view_covers( new_view, chunk );
        if ( was == now ) {
            // Still (un)covered: only the distance ordering can have changed.
            auto it = this->chunks.find( chunk );
            if ( it != this->chunks.end( ) ) {
                it->second.owner_fd = this->pick_owner( it->second, chunk );
            }
        } else if ( now ) {
            // Newly covered by this client.
            ServerChunkEntities &ce = this->chunks[ chunk ];
            if ( ce.viewers.empty( ) && ce.entities.empty( ) && ce.owner_fd < 0 ) {
                // First cover: pull the persisted entities into records.
                this->load_chunk_entities( ce, chunk );
                for ( const auto &pair : ce.entities ) {
                    this->entity_chunk[ pair.first ] = chunk;
                }
            }
            ce.viewers.insert( fd );
            ce.owner_fd = this->pick_owner( ce, chunk );
        } else {
            // This client stopped covering the chunk.
            auto it = this->chunks.find( chunk );
            if ( it != this->chunks.end( ) ) {
                const uint8_t was_role = this->role_of( fd, chunk );
                it->second.viewers.erase( fd );
                if ( it->second.viewers.empty( ) ) {
                    // Nobody covers it: persist+evict, then tell this client
                    // directly — uncover_chunk wiped the role bookkeeping
                    // apply_role_delta would have diffed against.
                    this->uncover_chunk( chunk );
                    if ( was_role != NET_ROLE_NONE ) {
                        this->send_chunk_role( server, fd, chunk, NET_ROLE_NONE );
                    }
                    continue;
                }
                it->second.owner_fd = this->pick_owner( it->second, chunk );
            }
        }
        // Push role deltas to the changed client and every current viewer
        // (a departing viewer's own delta is handled above on uncover).
        this->apply_role_delta( server, fd, chunk );
        const auto it = this->chunks.find( chunk );
        if ( it != this->chunks.end( ) ) {
            for ( int other : it->second.viewers ) {
                this->apply_role_delta( server, other, chunk );
            }
        }
    }
}

void ServerEntities::on_client_connected( Server &server, int client_fd ) {
    const uint32_t session = this->next_session_id++;
    if ( this->next_session_id == 0 ) {
        this->next_session_id = 1;
    }
    this->session_ids[ client_fd ] = session;
    NetWelcomePayload p{ session };
    PacketWriter w;
    net_serialize_welcome( w, p );
    server.queue_message( client_fd, NetMsgType::WELCOME, client_fd, w.take_buf( ) );
}

void ServerEntities::on_client_disconnected( Server &server, int client_fd ) {
    const auto vit = this->clients.find( client_fd );
    EntityClientView view;
    if ( vit != this->clients.end( ) ) {
        view = vit->second;
        this->clients.erase( vit );
    }
    this->session_ids.erase( client_fd );
    this->client_roles.erase( client_fd );
    if ( view.valid ) {
        // Drop the client from every chunk it covered; recompute owners for
        // the survivors and persist chunks nobody covers any more.
        EntityClientView empty_view;
        this->recompute_boxes( server, client_fd, view, empty_view );
    }
}

void ServerEntities::on_player_position( Server &server, int client_fd, float x, float y, float z ) {
    EntityClientView &v = this->clients[ client_fd ];
    const glm::ivec3 chunk = chunk_of_pos( x, y, z );
    if ( v.has_player_chunk && v.player_chunk == chunk ) {
        return;
    }
    const EntityClientView old_view = v;
    v.player_chunk = chunk;
    v.has_player_chunk = true;
    if ( v.valid ) {
        // Distances into every covered chunk changed; owners may flip.
        this->recompute_boxes( server, client_fd, old_view, v );
    }
}

ServerChunkEntities *ServerEntities::find_entity_chunk( NetEntityId id, glm::ivec3 *out_chunk ) {
    const auto cit = this->entity_chunk.find( id );
    if ( cit == this->entity_chunk.end( ) ) {
        return nullptr;
    }
    auto it = this->chunks.find( cit->second );
    if ( it == this->chunks.end( ) || it->second.entities.find( id ) == it->second.entities.end( ) ) {
        return nullptr;
    }
    if ( out_chunk != nullptr ) {
        *out_chunk = cit->second;
    }
    return &it->second;
}

bool ServerEntities::on_client_message( Server &server, int client_fd, NetMsgType type, const std::vector<uint8_t> &payload ) {
    PacketReader r( payload.data( ), payload.size( ) );

    if ( type == NetMsgType::VIEW ) {
        NetViewPayload p;
        if ( !net_deserialize_view( r, p ) ) {
            se_pr_debug( "Malformed VIEW from:%d", client_fd );
            return true;
        }
        EntityClientView &v = this->clients[ client_fd ];
        const EntityClientView old_view = v;
        v.valid = true;
        v.center = glm::ivec3( p.cx, p.cy, p.cz );
        v.rx = std::min<int>( p.radius_xz, NET_ENTITY_RADIUS_XZ );
        v.ry = std::min<int>( p.radius_y, NET_ENTITY_RADIUS_Y );
        this->recompute_boxes( server, client_fd, old_view, v );
        return true;
    }

    if ( type == NetMsgType::ENTITY_SPAWN ) {
        NetEntityRecord rec;
        if ( !net_read_entity_record( r, rec ) || rec.id == 0 ) {
            se_pr_debug( "Malformed ENTITY_SPAWN from:%d", client_fd );
            return true;
        }
        const glm::ivec3 chunk = chunk_of_pos( rec.x, rec.y, rec.z );
        const auto it = this->chunks.find( chunk );
        if ( it == this->chunks.end( ) ) {
            // Uncovered: persist straight to the sidecar and stop.
            this->persist_stray_entity( rec, chunk );
            return true;
        }
        it->second.entities[ rec.id ] = rec;
        this->entity_chunk[ rec.id ] = chunk;
        this->mark_dirty( it->second, rec.kind );
        PacketWriter w;
        net_write_entity_record( w, rec );
        for ( int fd : it->second.viewers ) {
            if ( fd != client_fd ) {
                server.queue_message( fd, NetMsgType::ENTITY_SPAWN, client_fd, w.buf( ) );
            }
        }
        return true;
    }

    if ( type == NetMsgType::ENTITY_STATE ) {
        NetEntityStatePayload p;
        if ( !net_deserialize_entity_state( r, p ) ) {
            se_pr_debug( "Malformed ENTITY_STATE from:%d", client_fd );
            return true;
        }
        // Per-recipient batched relays.
        std::map<int, NetEntityStatePayload> relays;
        for ( const NetEntityStateEntry &e : p.entries ) {
            glm::ivec3 cur_chunk;
            ServerChunkEntities *ce = find_entity_chunk( e.id, &cur_chunk );
            if ( ce == nullptr || ce->owner_fd != client_fd ) {
                continue; // only the owner of the entity's chunk may move it
            }
            NetEntityRecord &rec = ce->entities[ e.id ];
            rec.x = e.x;
            rec.y = e.y;
            rec.z = e.z;
            rec.vx = e.vx;
            rec.vy = e.vy;
            rec.vz = e.vz;
            rec.yaw = e.yaw;
            rec.count = e.count;
            rec.health = e.health;
            rec.flags = e.flags;
            this->mark_dirty( *ce, rec.kind );

            const glm::ivec3 new_chunk = chunk_of_pos( e.x, e.y, e.z );
            if ( new_chunk != cur_chunk ) {
                // The entity crossed a chunk boundary: move the record.
                NetEntityRecord moved = rec;
                ce->entities.erase( e.id );
                this->entity_chunk.erase( e.id );
                auto nit = this->chunks.find( new_chunk );
                if ( nit == this->chunks.end( ) ) {
                    // New chunk uncovered: persist it there and drop it from
                    // the sender's simulation.
                    this->persist_stray_entity( moved, new_chunk );
                    NetEntityRemovePayload rem{ e.id, NET_REMOVE_LEFT_VIEW };
                    PacketWriter wr;
                    net_serialize_entity_remove( wr, rem );
                    server.queue_message( client_fd, NetMsgType::ENTITY_REMOVE, 0, wr.take_buf( ) );
                } else {
                    nit->second.entities[ e.id ] = moved;
                    this->entity_chunk[ e.id ] = new_chunk;
                    this->mark_dirty( nit->second, moved.kind );
                    // Viewers gain/lose the entity as their coverage differs.
                    PacketWriter ws;
                    net_write_entity_record( ws, moved );
                    NetEntityRemovePayload rem{ e.id, NET_REMOVE_LEFT_VIEW };
                    PacketWriter wr;
                    net_serialize_entity_remove( wr, rem );
                    const auto oit = this->chunks.find( cur_chunk );
                    for ( int fd : nit->second.viewers ) {
                        if ( fd != client_fd && ( oit == this->chunks.end( ) || !oit->second.viewers.count( fd ) ) ) {
                            server.queue_message( fd, NetMsgType::ENTITY_SPAWN, client_fd, ws.buf( ) );
                        }
                    }
                    if ( oit != this->chunks.end( ) ) {
                        for ( int fd : oit->second.viewers ) {
                            if ( fd != client_fd && !nit->second.viewers.count( fd ) ) {
                                server.queue_message( fd, NetMsgType::ENTITY_REMOVE, 0, wr.buf( ) );
                            }
                        }
                    }
                    // Ownership transfer when the new chunk has a different owner.
                    if ( nit->second.owner_fd != client_fd ) {
                        NetEntityTransferPayload adopt{ moved, 1 };
                        PacketWriter wa;
                        net_serialize_entity_transfer( wa, adopt );
                        if ( nit->second.owner_fd >= 0 ) {
                            server.queue_message( nit->second.owner_fd, NetMsgType::ENTITY_TRANSFER, 0, wa.take_buf( ) );
                        }
                        NetEntityTransferPayload drop{ moved, 0 };
                        PacketWriter wd;
                        net_serialize_entity_transfer( wd, drop );
                        server.queue_message( client_fd, NetMsgType::ENTITY_TRANSFER, 0, wd.take_buf( ) );
                    }
                }
            }
            // Relay the state to the other viewers of the entity's (possibly
            // new) chunk.
            const auto dit = this->chunks.find( new_chunk );
            if ( dit != this->chunks.end( ) ) {
                for ( int fd : dit->second.viewers ) {
                    if ( fd != client_fd ) {
                        relays[ fd ].entries.push_back( e );
                    }
                }
            }
        }
        for ( auto &pair : relays ) {
            PacketWriter w;
            net_serialize_entity_state( w, pair.second );
            server.queue_message( pair.first, NetMsgType::ENTITY_STATE, client_fd, w.take_buf( ) );
        }
        return true;
    }

    if ( type == NetMsgType::ENTITY_REMOVE ) {
        NetEntityRemovePayload p;
        if ( !net_deserialize_entity_remove( r, p ) ) {
            se_pr_debug( "Malformed ENTITY_REMOVE from:%d", client_fd );
            return true;
        }
        glm::ivec3 chunk;
        ServerChunkEntities *ce = find_entity_chunk( p.id, &chunk );
        if ( ce == nullptr || ce->owner_fd != client_fd ) {
            return true; // only the owner may remove
        }
        const NetEntityKind kind = ce->entities[ p.id ].kind;
        ce->entities.erase( p.id );
        this->entity_chunk.erase( p.id );
        this->mark_dirty( *ce, kind );
        PacketWriter w;
        net_serialize_entity_remove( w, p );
        for ( int fd : ce->viewers ) {
            if ( fd != client_fd ) {
                server.queue_message( fd, NetMsgType::ENTITY_REMOVE, client_fd, w.buf( ) );
            }
        }
        return true;
    }

    if ( type == NetMsgType::PICKUP_REQUEST ) {
        NetPickupRequestPayload p;
        if ( !net_deserialize_pickup_request( r, p ) ) {
            se_pr_debug( "Malformed PICKUP_REQUEST from:%d", client_fd );
            return true;
        }
        glm::ivec3 chunk;
        ServerChunkEntities *ce = find_entity_chunk( p.id, &chunk );
        NetEntityRecord *rec = nullptr;
        if ( ce != nullptr ) {
            const auto it = ce->entities.find( p.id );
            if ( it != ce->entities.end( ) && it->second.kind == NetEntityKind::ITEM ) {
                rec = &it->second;
            }
        }
        int32_t granted = 0;
        uint32_t item = 0;
        if ( rec != nullptr ) {
            item = rec->type;
            granted = p.room > 0 ? std::min<int32_t>( p.room, rec->count ) : 0;
            if ( granted >= rec->count ) {
                // Whole stack picked up: remove everywhere, requester included.
                granted = rec->count;
                ce->entities.erase( p.id );
                this->entity_chunk.erase( p.id );
                ce->items_dirty = true;
                NetEntityRemovePayload rem{ p.id, NET_REMOVE_PICKED_UP };
                PacketWriter w;
                net_serialize_entity_remove( w, rem );
                for ( int fd : ce->viewers ) {
                    server.queue_message( fd, NetMsgType::ENTITY_REMOVE, 0, w.buf( ) );
                }
            } else if ( granted > 0 ) {
                // Partial: reduce the count and push a server-originated
                // state (header player_id 0) to every viewer.
                rec->count -= granted;
                ce->items_dirty = true;
                NetEntityStatePayload sp;
                NetEntityStateEntry e;
                e.id = rec->id;
                e.kind = static_cast<uint8_t>( rec->kind );
                e.x = rec->x;
                e.y = rec->y;
                e.z = rec->z;
                e.vx = rec->vx;
                e.vy = rec->vy;
                e.vz = rec->vz;
                e.yaw = rec->yaw;
                e.count = rec->count;
                e.health = rec->health;
                e.flags = rec->flags;
                sp.entries.push_back( e );
                PacketWriter w;
                net_serialize_entity_state( w, sp );
                for ( int fd : ce->viewers ) {
                    server.queue_message( fd, NetMsgType::ENTITY_STATE, 0, w.buf( ) );
                }
            }
        }
        NetPickupResultPayload result{ p.id, item, granted };
        PacketWriter w;
        net_serialize_pickup_result( w, result );
        server.queue_message( client_fd, NetMsgType::PICKUP_RESULT, 0, w.take_buf( ) );
        return true;
    }

    if ( type == NetMsgType::ENTITY_ACTION ) {
        NetEntityActionPayload p;
        if ( !net_deserialize_entity_action( r, p ) ) {
            se_pr_debug( "Malformed ENTITY_ACTION from:%d", client_fd );
            return true;
        }
        glm::ivec3 chunk;
        ServerChunkEntities *ce = find_entity_chunk( p.id, &chunk );
        if ( ce != nullptr && ce->owner_fd >= 0 ) {
            PacketWriter w;
            net_serialize_entity_action( w, p );
            server.queue_message( ce->owner_fd, NetMsgType::ENTITY_ACTION, client_fd, w.take_buf( ) );
        }
        return true;
    }

    if ( type == NetMsgType::PLAYER_HIT ) {
        NetPlayerHitPayload p;
        if ( !net_deserialize_player_hit( r, p ) ) {
            se_pr_debug( "Malformed PLAYER_HIT from:%d", client_fd );
            return true;
        }
        if ( p.target_player_id >= 0 && p.target_player_id < MAX_CLIENT_FDS &&
             server.get_data_if_client_connected( p.target_player_id ) != NULL ) {
            PacketWriter w;
            net_serialize_player_hit( w, p );
            server.queue_message( p.target_player_id, NetMsgType::PLAYER_HIT, client_fd, w.take_buf( ) );
        }
        return true;
    }

    if ( type == NetMsgType::EXPLOSION ) {
        NetExplosionPayload p;
        if ( !net_deserialize_explosion( r, p ) ) {
            se_pr_debug( "Malformed EXPLOSION from:%d", client_fd );
            return true;
        }
        PacketWriter w;
        net_serialize_explosion( w, p );
        for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
            if ( fd != client_fd && server.get_data_if_client_connected( fd ) != NULL ) {
                server.queue_message( fd, NetMsgType::EXPLOSION, client_fd, w.buf( ) );
            }
        }
        return true;
    }

    return false;
}

void ServerEntities::flush_all( ) {
    for ( auto &pair : this->chunks ) {
        this->persist_chunk_entities( pair.second, pair.first );
    }
}
