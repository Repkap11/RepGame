// NetEntities: client side of entity sync. See net_entities.hpp.

#include "common/net_entities.hpp"

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_bodies.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/chunk.hpp"
#include "common/item_entities.hpp"
#include "common/mp_test.hpp"
#include "common/multiplayer.hpp"
#include "common/world.hpp"

#include <algorithm>

static glm::ivec3 chunk_of_dpos( const glm::dvec3 &pos ) {
    return glm::ivec3( glm::floor( pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
}

bool NetEntities::chunk_live( RepGameState &state, const glm::ivec3 &chunk ) const {
    const glm::ivec3 block = chunk * CHUNK_SIZE_I + CHUNK_SIZE_I / 2;
    const Chunk *c = state.world.get_loaded_chunk( block );
    return c != nullptr && c->load_state == CHUNK_LIVE;
}

int NetEntities::role_of( const glm::ivec3 &chunk ) const {
    const auto it = this->roles.find( chunk );
    return it == this->roles.end( ) ? static_cast<int>( NET_ROLE_NONE ) : it->second;
}

bool NetEntities::owns_chunk( const glm::ivec3 &chunk ) const {
    return this->role_of( chunk ) == NET_ROLE_OWNER;
}

bool NetEntities::views_chunk( const glm::ivec3 &chunk ) const {
    return this->role_of( chunk ) != NET_ROLE_NONE;
}

NetEntityId NetEntities::mint_id( ) {
    if ( !this->welcomed ) {
        return 0;
    }
    const uint32_t counter = this->entity_counter++;
    if ( this->entity_counter == 0 ) {
        this->entity_counter = 1;
    }
    return ( static_cast<NetEntityId>( this->session_id ) << 32 ) | counter;
}

void NetEntities::reset( ) {
    this->session_id = 0;
    this->entity_counter = 1;
    this->welcomed = false;
    this->roles.clear( );
    this->pending_roles.clear( );
    this->gone_ids.clear( );
    this->view_sent = false;
    this->view_rx = -1;
    this->view_ry = -1;
}

void NetEntities::send_view( int cx, int cy, int cz, int rx, int ry ) {
    NetViewPayload p;
    p.cx = cx;
    p.cy = cy;
    p.cz = cz;
    p.radius_xz = static_cast<uint8_t>( rx );
    p.radius_y = static_cast<uint8_t>( ry );
    this->mp->send( NetMsgType::VIEW, p );
    this->view_sent = true;
    this->view_center = glm::ivec3( cx, cy, cz );
    this->view_rx = rx;
    this->view_ry = ry;
}

NetEntities::PendingRole *NetEntities::pending_for( const glm::ivec3 &chunk ) {
    for ( PendingRole &p : this->pending_roles ) {
        if ( p.chunk == chunk ) {
            return &p;
        }
    }
    return nullptr;
}

void NetEntities::send_spawn( const NetEntityRecord &rec ) {
    if ( !this->welcomed ) {
        return;
    }
    // Own spawn into a chunk whose grant is still pending: the grant's
    // record snapshot predates this entity, so exempt it from the resync wipe.
    if ( PendingRole *p = this->pending_for( chunk_of_dpos( glm::dvec3( rec.x, rec.y, rec.z ) ) ) ) {
        p->late_ids.insert( rec.id );
    }
    this->mp->send( NetMsgType::ENTITY_SPAWN, rec );
}

void NetEntities::send_remove( NetEntityId id, uint8_t reason ) {
    if ( !this->welcomed ) {
        return;
    }
    this->gone_ids.insert( id );
    NetEntityRemovePayload p{ id, reason };
    this->mp->send( NetMsgType::ENTITY_REMOVE, p );
}

void NetEntities::send_pickup_request( NetEntityId id, int32_t room ) {
    if ( !this->welcomed ) {
        return;
    }
    NetPickupRequestPayload p{ id, room };
    this->mp->send( NetMsgType::PICKUP_REQUEST, p );
}

void NetEntities::send_entity_action( const NetEntityActionPayload &p ) {
    if ( !this->welcomed ) {
        return;
    }
    this->mp->send( NetMsgType::ENTITY_ACTION, p );
}

void NetEntities::send_explosion( double x, double y, double z, float radius ) {
    if ( !this->welcomed ) {
        return;
    }
    NetExplosionPayload p{ x, y, z, radius };
    this->mp->send( NetMsgType::EXPLOSION, p );
}

void NetEntities::send_chunk_populated( const glm::ivec3 &chunk ) {
    if ( !this->welcomed ) {
        return;
    }
    NetChunkPopulatedPayload p{ chunk.x, chunk.y, chunk.z };
    this->mp->send( NetMsgType::CHUNK_POPULATED, p );
}

void NetEntities::send_player_hit( const int32_t target_player_id, const glm::dvec3 &from, const float damage ) {
    if ( !this->welcomed ) {
        return;
    }
    NetPlayerHitPayload p{ target_player_id, from.x, from.y, from.z, damage };
    this->mp->send( NetMsgType::PLAYER_HIT, p );
}

void NetEntities::apply_chunk_role( RepGameState &state, const PendingRole &grant ) {
    if ( grant.role == NET_ROLE_NONE ) {
        this->roles.erase( grant.chunk );
    } else {
        this->roles[ grant.chunk ] = grant.role;
    }
    // The record snapshot predates apply time for pending grants: drop ids
    // removed since the grant arrived (dead entities must not resurrect),
    // and keep post-grant spawns (late_ids) out of the resync wipe.
    std::vector<NetEntityRecord> records;
    records.reserve( grant.records.size( ) );
    for ( const NetEntityRecord &rec : grant.records ) {
        if ( !this->gone_ids.count( rec.id ) ) {
            records.push_back( rec );
        }
    }
    state.world.items.net_apply_chunk_role( grant.chunk, grant.role, records, grant.late_ids );
    state.world.mobs.net_apply_chunk_role( state, grant.chunk, grant.role, grant.populated, records, grant.removed_natural, grant.late_ids );
    state.world.bodies.net_apply_chunk_role( grant.chunk, grant.role, records, grant.late_ids );
}

bool NetEntities::handle( RepGameState &state, NetMsgType type, int32_t player_id, PacketReader &r ) {
    switch ( type ) {
        case NetMsgType::WELCOME: {
            NetWelcomePayload p;
            if ( !net_deserialize_welcome( r, p ) ) {
                pr_debug( "Malformed WELCOME" );
                return true;
            }
            this->session_id = p.session_id;
            this->welcomed = true;
            this->view_sent = false; // next tick advertises the view
            mp_test::event( &state, "welcome", "\"session\":\"%u\"", p.session_id );
            return true;
        }
        case NetMsgType::CHUNK_ROLE: {
            NetChunkRolePayload p;
            if ( !net_deserialize_chunk_role( r, p ) ) {
                pr_debug( "Malformed CHUNK_ROLE" );
                return true;
            }
            PendingRole grant;
            grant.chunk = glm::ivec3( p.cx, p.cy, p.cz );
            grant.role = p.role;
            grant.populated = p.populated;
            grant.records = std::move( p.records );
            grant.removed_natural = std::move( p.removed_natural );
            // The latest grant supersedes anything still queued for this
            // chunk — drop the stale entry before queuing/applying.
            for ( size_t i = 0; i < this->pending_roles.size( ); i++ ) {
                if ( this->pending_roles[ i ].chunk == grant.chunk ) {
                    this->pending_roles.erase( this->pending_roles.begin( ) + static_cast<long>( i ) );
                    break;
                }
            }
            if ( grant.role != NET_ROLE_NONE && !this->chunk_live( state, grant.chunk ) ) {
                // The chunk isn't live locally yet; apply when it finalizes.
                for ( PendingRole &old : this->pending_roles ) {
                    if ( old.chunk == grant.chunk ) {
                        old = std::move( grant );
                        return true;
                    }
                }
                this->pending_roles.push_back( std::move( grant ) );
                return true;
            }
            this->apply_chunk_role( state, grant );
            return true;
        }
        case NetMsgType::ENTITY_SPAWN: {
            NetEntityRecord rec;
            if ( !net_read_entity_record( r, rec ) ) {
                pr_debug( "Malformed ENTITY_SPAWN" );
                return true;
            }
            const glm::ivec3 rec_chunk = chunk_of_dpos( glm::dvec3( rec.x, rec.y, rec.z ) );
            // This spawn postdates any pending grant's record snapshot for
            // its chunk — exempt it from that grant's resync wipe.
            if ( PendingRole *pend = this->pending_for( rec_chunk ) ) {
                pend->late_ids.insert( rec.id );
            }
            if ( rec.kind == NetEntityKind::ITEM ) {
                const bool mirror = !this->owns_chunk( rec_chunk );
                state.world.items.net_apply_spawn( rec, mirror );
            } else if ( rec.kind == NetEntityKind::MOB ) {
                const bool mirror = !this->owns_chunk( rec_chunk );
                state.world.mobs.net_apply_spawn( rec, mirror );
            } else if ( rec.kind == NetEntityKind::BODY ) {
                // Bodies are causer-simulated: a relayed spawn is always a
                // mirror here (unless we already own that net_id).
                state.world.bodies.net_apply_spawn( rec, true, state.tick_number );
            }
            return true;
        }
        case NetMsgType::ENTITY_STATE: {
            NetEntityStatePayload p;
            if ( !net_deserialize_entity_state( r, p ) ) {
                pr_debug( "Malformed ENTITY_STATE" );
                return true;
            }
            for ( const NetEntityStateEntry &e : p.entries ) {
                if ( e.kind == static_cast<uint8_t>( NetEntityKind::ITEM ) ) {
                    state.world.items.net_apply_state( e );
                } else if ( e.kind == static_cast<uint8_t>( NetEntityKind::MOB ) ) {
                    state.world.mobs.net_apply_state( e );
                } else if ( e.kind == static_cast<uint8_t>( NetEntityKind::BODY ) ) {
                    state.world.bodies.net_apply_state( e );
                }
            }
            return true;
        }
        case NetMsgType::ENTITY_REMOVE: {
            NetEntityRemovePayload p;
            if ( !net_deserialize_entity_remove( r, p ) ) {
                pr_debug( "Malformed ENTITY_REMOVE" );
                return true;
            }
            this->gone_ids.insert( p.id );
            for ( PendingRole &pend : this->pending_roles ) {
                pend.late_ids.erase( p.id );
            }
            // One kind owns the id; try all three.
            state.world.items.net_apply_remove( p.id );
            state.world.mobs.net_apply_remove( p.id );
            state.world.bodies.net_apply_remove( p.id );
            return true;
        }
        case NetMsgType::ENTITY_TRANSFER: {
            NetEntityTransferPayload p;
            if ( !net_deserialize_entity_transfer( r, p ) ) {
                pr_debug( "Malformed ENTITY_TRANSFER" );
                return true;
            }
            mp_test::event( &state, "entity_transfer", "\"id\":\"%llu\",\"kind\":%d,\"adopt\":%d",
                            static_cast<unsigned long long>( p.record.id ), static_cast<int>( p.record.kind ), p.adopt );
            if ( p.adopt ) {
                if ( PendingRole *pend = this->pending_for( chunk_of_dpos( glm::dvec3( p.record.x, p.record.y, p.record.z ) ) ) ) {
                    pend->late_ids.insert( p.record.id );
                }
            }
            if ( p.record.kind == NetEntityKind::ITEM ) {
                if ( p.adopt ) {
                    // We now simulate it (owned, not a mirror).
                    state.world.items.net_apply_spawn( p.record, false );
                } else {
                    const glm::ivec3 chunk = chunk_of_dpos( glm::dvec3( p.record.x, p.record.y, p.record.z ) );
                    state.world.items.net_release_entity( p.record, this->views_chunk( chunk ) );
                }
            } else if ( p.record.kind == NetEntityKind::MOB ) {
                if ( p.adopt ) {
                    state.world.mobs.net_apply_spawn( p.record, false );
                } else {
                    const glm::ivec3 chunk = chunk_of_dpos( glm::dvec3( p.record.x, p.record.y, p.record.z ) );
                    state.world.mobs.net_release_entity( p.record, this->views_chunk( chunk ) );
                }
            } else if ( p.record.kind == NetEntityKind::BODY ) {
                // Sim handover after the previous sim disconnected.
                if ( p.adopt ) {
                    state.world.bodies.net_apply_transfer( state, p.record );
                }
            }
            return true;
        }
        case NetMsgType::PICKUP_RESULT: {
            NetPickupResultPayload p;
            if ( !net_deserialize_pickup_result( r, p ) ) {
                pr_debug( "Malformed PICKUP_RESULT" );
                return true;
            }
            mp_test::event( &state, "pickup_result", "\"id\":\"%llu\",\"item\":%d,\"granted\":%d",
                            static_cast<unsigned long long>( p.id ), p.item, p.granted );
            ItemEntities::ItemEntity *it = state.world.items.find_by_net_id( p.id );
            if ( p.granted > 0 ) {
                RepGame::give_item_to_player( state, static_cast<ItemID>( p.item ), p.granted );
                audio_play_sfx_gain( "pop", 0.5f );
                state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
            }
            if ( it != nullptr ) {
                // The ENTITY_REMOVE (full pickup) or updated ENTITY_STATE
                // (partial) resolves the entity; just unhide.
                it->pickup_pending = false;
            }
            return true;
        }
        case NetMsgType::PLAYER_HIT: {
            NetPlayerHitPayload p;
            if ( !net_deserialize_player_hit( r, p ) ) {
                pr_debug( "Malformed PLAYER_HIT" );
                return true;
            }
            mp_test::event( &state, "player_hit_received", "\"damage\":%g", p.damage );
            RepGame::player_hurt( state, glm::dvec3( p.fx, p.fy, p.fz ), p.damage );
            return true;
        }
        case NetMsgType::EXPLOSION: {
            NetExplosionPayload p;
            if ( !net_deserialize_explosion( r, p ) ) {
                pr_debug( "Malformed EXPLOSION" );
                return true;
            }
            mp_test::event( &state, "explosion_received", "\"x\":%g,\"y\":%g,\"z\":%g,\"radius\":%g",
                            p.x, p.y, p.z, p.radius );
            // A remote client's blast: play its audio and shove the entities
            // we own. Blocks arrive separately via BLOCK_UPDATE; mirrors are
            // skipped inside the impulse loops (their sim got this too).
            const glm::dvec3 center( p.x, p.y, p.z );
            if ( glm::length( center - state.camera.pos ) < 96.0 ) {
                audio_play_sfx( "explode" );
            }
            state.world.bodies.impulse( center, p.radius, TNT_IMPULSE_STRENGTH );
            state.world.items.impulse( center, p.radius, TNT_IMPULSE_STRENGTH );
            return true;
        }
        case NetMsgType::ENTITY_ACTION: {
            NetEntityActionPayload p;
            if ( !net_deserialize_entity_action( r, p ) ) {
                pr_debug( "Malformed ENTITY_ACTION" );
                return true;
            }
            // Forwarded to us as the owner of the entity's chunk (mob HITs).
            state.world.mobs.net_apply_action( state, p );
            return true;
        }
        default:
            return false;
    }
}

void NetEntities::tick( RepGameState &state ) {
    if ( !this->mp->is_active( ) ) {
        if ( this->welcomed || !this->roles.empty( ) || this->view_sent ) {
            this->reset( );
            // Mirror bodies would freeze mid-air without their sim.
            state.world.bodies.net_disconnect( );
        }
        return;
    }

    // Apply queued role grants whose chunks have become live.
    for ( size_t i = 0; i < this->pending_roles.size( ); ) {
        if ( this->chunk_live( state, this->pending_roles[ i ].chunk ) ) {
            PendingRole grant = std::move( this->pending_roles[ i ] );
            this->pending_roles.erase( this->pending_roles.begin( ) + static_cast<long>( i ) );
            this->apply_chunk_role( state, grant );
        } else {
            i++;
        }
    }
    // Hold VIEW until the player's real position is known: advertising the
    // pre-spawn default would grab ownership of chunks we never stand in.
    if ( !this->welcomed || state.spawn_pending ) {
        return;
    }

    // VIEW: re-advertise when the player's chunk or the active render
    // radius changed (clamped to the protocol window).
    const glm::ivec3 center = chunk_of_dpos( state.camera.pos );
    const int rx = std::max( 1, std::min( static_cast<int>( state.input.render_distance ), NET_ENTITY_RADIUS_XZ ) );
    const int ry = std::min( CHUNK_RADIUS_Y, NET_ENTITY_RADIUS_Y );
    if ( !this->view_sent || center != this->view_center || rx != this->view_rx || ry != this->view_ry ) {
        this->send_view( center.x, center.y, center.z, rx, ry );
        mp_test::event( &state, "view_sent", "\"center\":[%d,%d,%d],\"rx\":%d,\"ry\":%d", center.x, center.y, center.z, rx, ry );
    }

    // Owned entities stream state at ~24 Hz.
    if ( state.tick_number % NET_ENTITY_STATE_INTERVAL_TICKS == 0 ) {
        NetEntityStatePayload p;
        state.world.items.collect_net_state( *this, p.entries, state.tick_number );
        state.world.mobs.collect_net_state( *this, p.entries, state.tick_number );
        state.world.bodies.collect_net_state( p.entries, state.tick_number );
        if ( !p.entries.empty( ) ) {
            this->mp->send( NetMsgType::ENTITY_STATE, p );
        }
    }
}
