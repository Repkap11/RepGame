#include "common/vehicles.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

#include "common/RepGame.hpp"
#include "common/block.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include "common/game/game_internal.hpp" // repgame_item_drop_hop
#include "common/mobs.hpp" // mob_bone_worlds / mob_cube_matrix + mob_models
#include "common/mp_test.hpp"
#include "common/net_entities.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/vehicle_physics.hpp" // per-physics dispatch (vehicle_tick_rail, ...)
#include "common/world.hpp"

// Survival hits accumulate 1 damage each and decay at this rate per tick —
// three hits inside ~2 ticks of each other break (decay 1 per ~0.7 s).
static constexpr float CART_DAMAGE_PER_HIT = 1.0f;
static constexpr float CART_DAMAGE_DECAY = 2.0f / static_cast<float>( UPS_RATE );
static constexpr int CART_HURT_WOBBLE_TICKS = UPS_RATE / 2;

void Vehicles::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance ) {
    this->render_chain.init( vbl_object_vertex, vbl_mob_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_inited = true;
}

void Vehicles::init_storage( MapStorage *s ) {
    this->storage = s;
}

uint32_t Vehicles::spawn( const VehicleType type, const glm::dvec3 &pos, float yaw, int persistent, bool from_net ) {
    if ( this->vehicles.size( ) >= MAX_VEHICLES || !vehicle_type_valid( type ) ) {
        return 0;
    }
    // Multiplayer: the spawn announces through the server, so it needs a
    // net id — mint_id returns 0 before WELCOME; refuse then (and while
    // mp mode has no live session). Never-connected sessions spawn
    // local-only as before.
    NetEntityId nid = 0;
    if ( this->net != nullptr && !from_net ) {
        nid = this->net->mint_id( );
        if ( nid == 0 ) {
            return 0;
        }
    } else if ( this->mp_mode && !from_net ) {
        return 0;
    }
    Vehicle c;
    c.id = this->next_id++;
    c.type = type;
    c.pos = c.prev_pos = pos;
    c.yaw = c.prev_yaw = yaw;
    c.net_yaw = yaw;
    c.persistent = persistent;
    if ( this->render_inited ) {
        for ( int i = 0; i < mob_models[ vehicle_type_def( type ).model ].cube_count; i++ ) {
            const std::pair<entt::entity, MobPartInstance &> data = this->render_chain.create_instance( );
            c.parts.push_back( data.first );
        }
    }
    this->vehicles.push_back( c );
    Vehicle &back = this->vehicles.back( );
    back.net.chunk = glm::ivec3( glm::floor( pos / glm::dvec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) ) );
    if ( nid != 0 ) {
        back.net.id = nid;
        NetEntityRecord rec;
        rec.id = nid;
        rec.kind = NetEntityKind::VEHICLE;
        rec.type = type;
        rec.x = back.pos.x;
        rec.y = back.pos.y;
        rec.z = back.pos.z;
        rec.vx = static_cast<float>( back.vel.x );
        rec.vy = static_cast<float>( back.vel.y );
        rec.vz = static_cast<float>( back.vel.z );
        rec.yaw = back.yaw;
        rec.count = 0; // rider session id — empty cart
        this->net->send_spawn( rec );
    }
    return c.id;
}

uint32_t Vehicles::spawn_on_rail( World &world, const glm::ivec3 &rail_cell, float player_yaw ) {
    glm::dvec3 pos;
    float yaw = 0.0f;
    if ( !vehicle_rail_spawn_pose( world, rail_cell, player_yaw, &pos, &yaw ) ) {
        return 0;
    }
    const uint32_t id = this->spawn( VEHICLE_MINECART, pos, yaw );
    if ( id != 0 ) {
        this->vehicles.back( ).on_rail = true;
    }
    return id;
}

bool Vehicles::vehicle_chunk_live( World &world, const Vehicle &c ) const {
    const Chunk *chunk = world.get_loaded_chunk( glm::ivec3( glm::floor( c.pos ) ) );
    return chunk != nullptr && chunk->load_state == CHUNK_LIVE;
}

// Generic per-tick loop: prev snapshot, freeze/chunk-live gating,
// hurt/damage decay, mirror easing, then the owned sim dispatched on the
// type's physics (RAIL -> vehicle_tick_rail in vehicle_rail.cpp).
void Vehicles::tick( RepGameState &state, const VehicleTickInput &in ) {
    World &world = state.world;
    this->tick_pending_mount( state );
    const bool freeze = RepGame::capture_mode( );

    for ( Vehicle &c : this->vehicles ) {
        c.prev_pos = c.pos;
        c.prev_yaw = c.yaw;
        c.prev_pitch = c.pitch;
        if ( freeze || !vehicle_chunk_live( world, c ) ) {
            continue;
        }
        // The local rider flag only applies while we simulate the
        // vehicle; a remote rider's vehicle is a mirror and skips all of
        // this anyway.
        c.rider = !c.net.mirror && c.id != 0 && c.id == in.rider_vehicle;
        if ( c.hurt_ticks > 0 ) {
            c.hurt_ticks--;
        }
        if ( c.damage > 0.0f ) {
            c.damage = std::max( 0.0f, c.damage - CART_DAMAGE_DECAY );
        }

        if ( c.net.mirror ) {
            // Remote-owned: no physics, collisions, pushes or rail
            // stepping — ease toward the dead-reckoned streamed target.
            // Yaw is streamed; pitch is derived per physics (it is not
            // sent on the wire).
            c.net.pos += c.net.vel;
            const glm::dvec3 delta = c.net.pos - c.pos;
            if ( glm::length( delta ) > 4.0 ) {
                c.pos = c.net.pos;
            } else {
                c.pos += delta * 0.35;
            }
            c.vel = c.net.vel;
            c.yaw = c.net_yaw;
            switch ( vehicle_type_def( c.type ).physics ) {
                case VehiclePhysics::RAIL:
                    c.pitch = vehicle_rail_mirror_pitch( world, c );
                    break;
                case VehiclePhysics::WATER:
                    c.pitch = 0.0f;
                    break;
            }
        } else {
            switch ( vehicle_type_def( c.type ).physics ) {
                case VehiclePhysics::RAIL:
                    vehicle_tick_rail( state, c, in );
                    break;
                case VehiclePhysics::WATER:
                    vehicle_tick_boat( state, c, in );
                    break;
            }
        }
        // Paddle phase is render-local (never streamed): it advances with
        // horizontal speed while a boat is ridden — on a mirror vel is
        // the streamed velocity, on an owned boat the sim just wrote it.
        if ( vehicle_type_def( c.type ).physics == VehiclePhysics::WATER ) {
            const double speed = std::sqrt( c.vel.x * c.vel.x + c.vel.z * c.vel.z ) * UPS_RATE;
            if ( ( c.rider || c.rider_session != 0 ) && speed > 0.05 ) {
                c.paddle_phase += static_cast<float>( speed * 0.9 / UPS_RATE );
            }
        }
    }

    // Vehicle-vehicle collisions: overlapping boxes separate along the
    // line between them and exchange along-normal speeds (equal mass).
    // Mirrors take no part — their owner already resolved the contact.
    for ( size_t i = 0; !freeze && i < this->vehicles.size( ); i++ ) {
        for ( size_t j = i + 1; j < this->vehicles.size( ); j++ ) {
            Vehicle &a = this->vehicles[ i ];
            Vehicle &b = this->vehicles[ j ];
            if ( a.net.mirror || b.net.mirror || !vehicle_chunk_live( world, a ) || !vehicle_chunk_live( world, b ) ) {
                continue;
            }
            const VehicleTypeDef &adef = vehicle_type_def( a.type );
            const VehicleTypeDef &bdef = vehicle_type_def( b.type );
            const glm::dvec3 amin = vehicle_min( adef, a.pos ), amax = vehicle_max( adef, a.pos );
            const glm::dvec3 bmin = vehicle_min( bdef, b.pos ), bmax = vehicle_max( bdef, b.pos );
            if ( !( amin.x < bmax.x && amax.x > bmin.x && amin.y < bmax.y && amax.y > bmin.y && amin.z < bmax.z && amax.z > bmin.z ) ) {
                continue;
            }
            glm::dvec3 sep = ( a.pos - b.pos );
            sep.y = 0.0;
            double dist = glm::length( sep );
            glm::dvec3 n = dist > 1e-6 ? sep / dist : glm::dvec3( 1.0, 0.0, 0.0 );
            const double overlap = ( adef.width + bdef.width ) * 0.5 - dist;
            if ( overlap > 0.0 ) {
                a.pos += n * ( overlap * 0.5 );
                b.pos -= n * ( overlap * 0.5 );
            }
            // Equal-mass exchange along the collision normal.
            const double va = glm::dot( a.vel, n );
            const double vb = glm::dot( b.vel, n );
            a.vel += n * ( vb - va );
            b.vel += n * ( va - vb );
        }
    }

    // Owned-chunk leave-poll (~every 16 ticks), same cadence as items.
    if ( ++this->poll_count >= 16 ) {
        this->poll_count = 0;
        this->poll_owned( world );
    }
}

int Vehicles::raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const {
    int best = -1;
    double best_dist = max_dist;
    for ( size_t i = 0; i < this->vehicles.size( ); i++ ) {
        const Vehicle &c = this->vehicles[ i ];
        const VehicleTypeDef &def = vehicle_type_def( c.type );
        double d = 0.0;
        if ( RayTraversal::ray_aabb_entry( origin, dir, vehicle_min( def, c.pos ), vehicle_max( def, c.pos ), best_dist, &d ) && d < best_dist ) {
            best_dist = d;
            best = static_cast<int>( i );
        }
    }
    if ( best >= 0 && out_dist != nullptr ) {
        *out_dist = best_dist;
    }
    return best;
}

int Vehicles::resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                               double block_dist, double *out_dist ) const {
    double vehicle_dist = 0.0;
    const int vehicle = this->raycast( origin, dir, max_dist, &vehicle_dist );
    if ( vehicle < 0 || vehicle_dist >= block_dist ) {
        return -1;
    }
    if ( out_dist != nullptr ) {
        *out_dist = vehicle_dist;
    }
    return vehicle;
}

void Vehicles::remove_vehicle( size_t i ) {
    this->drop_render_parts( this->vehicles[ i ] );
    this->vehicles.erase( this->vehicles.begin( ) + static_cast<long>( i ) );
}

void Vehicles::send_net_remove( Vehicle &c, const uint8_t reason ) {
    // ENTITY_REMOVE from whoever simulates: a cart we ride in a foreign
    // chunk must still announce its removal (the rider is the sim fd).
    if ( this->net != nullptr && c.net.id != 0 && this->sim_here( c ) ) {
        this->net->send_remove( c.net.id, reason );
    }
}

bool Vehicles::hurt( int index, bool survival, bool *drop ) {
    if ( index < 0 || index >= static_cast<int>( this->vehicles.size( ) ) ) {
        return false;
    }
    Vehicle &c = this->vehicles[ index ];
    if ( !survival ) {
        if ( drop != nullptr ) {
            *drop = false;
        }
        this->send_net_remove( c, NET_REMOVE_DIED );
        remove_vehicle( index );
        return true;
    }
    c.damage += CART_DAMAGE_PER_HIT;
    c.hurt_ticks = CART_HURT_WOBBLE_TICKS;
    if ( c.damage >= 3.0f ) {
        if ( drop != nullptr ) {
            *drop = true;
        }
        this->send_net_remove( c, NET_REMOVE_DIED );
        remove_vehicle( index );
        return true;
    }
    return false;
}

// hurt() plus the drop side effects — the one place a broken owned
// vehicle drops its item, shared by local clicks and forwarded
// ENTITY_ACTIONs.
bool Vehicles::hurt_local( RepGameState &state, const int index, const bool survival ) {
    if ( index < 0 || index >= static_cast<int>( this->vehicles.size( ) ) ) {
        return false;
    }
    const ItemID drop_item = vehicle_type_def( this->vehicles[ index ].type ).item;
    const glm::dvec3 pos = this->vehicles[ index ].pos;
    bool drop = false;
    if ( this->hurt( index, survival, &drop ) && drop ) {
        state.world.items.spawn( drop_item, 1, pos + glm::dvec3( 0.0, 0.3, 0.0 ), repgame_item_drop_hop( ), 10 );
        return true;
    }
    return false;
}

int Vehicles::hit_interact( RepGameState &state, const int index, const bool survival, const glm::dvec3 &attacker_pos ) {
    if ( index < 0 || index >= static_cast<int>( this->vehicles.size( ) ) ) {
        return 0;
    }
    Vehicle &c = this->vehicles[ index ];
    if ( c.net.mirror && c.net.id != 0 ) {
        // Remote-owned: local wobble for feedback — the owner validates
        // the hit and streams the outcome back. Sent through the hub;
        // no-op when the session is gone.
        c.hurt_ticks = CART_HURT_WOBBLE_TICKS;
        if ( this->net != nullptr ) {
            NetEntityActionPayload act = { };
            act.id = c.net.id;
            act.action = NET_ACTION_HIT;
            act.damage = survival ? CART_DAMAGE_PER_HIT : 0.0f;
            act.ax = attacker_pos.x;
            act.ay = attacker_pos.y;
            act.az = attacker_pos.z;
            act.attacker_survival = survival ? 1 : 0;
            act.item = 0;
            this->net->send_entity_action( act );
        }
        return 2;
    }
    this->hurt_local( state, index, survival );
    return 1;
}

// --- Mount/dismount over the wire ---------------------------------------

// Right-click a cart in multiplayer: the server arbitrates the mount —
// send ENTITY_ACTION MOUNT and wait for the record's `count` to become
// our session. Returns false when the cart can't be requested (occupied
// or unsynced); an occupied cart still counts as "handled" so the click
// doesn't fall through to item use.
bool Vehicles::request_mount( RepGameState &state, const int index ) {
    if ( index < 0 || index >= static_cast<int>( this->vehicles.size( ) ) ) {
        return false;
    }
    Vehicle &c = this->vehicles[ index ];
    if ( this->net == nullptr ) {
        return false;
    }
    if ( c.rider_session != 0 || c.net.id == 0 ) {
        return c.rider_session != 0; // occupied: swallow the click
    }
    NetEntityActionPayload act = { };
    act.id = c.net.id;
    act.action = NET_ACTION_MOUNT;
    this->net->send_entity_action( act );
    this->pending_mount_id = c.net.id;
    this->pending_mount_tick = state.tick_number;
    mp_test::event( &state, "vehicle_mount_sent", "\"id\":\"%llu\"", (unsigned long long)c.net.id );
    return true;
}

// The mount completes when the server's rider field names our session
// (COUNT state, record in a grant, or adopt transfer). Refusal, removal
// of the cart, and a 3 s timeout clear the pending request.
void Vehicles::tick_pending_mount( RepGameState &state ) {
    if ( this->pending_mount_id == 0 ) {
        return;
    }
    Vehicle *c = this->find_by_net_id( this->pending_mount_id );
    if ( c == nullptr ) {
        this->pending_mount_id = 0; // removed under us
        return;
    }
    const uint32_t session = this->net != nullptr ? this->net->session( ) : 0;
    if ( session != 0 && c->rider_session == session && this->sim_here( *c ) ) {
        const uint32_t local_id = c->id;
        this->pending_mount_id = 0;
        repgame_vehicle_mount( state, local_id );
        mp_test::event( &state, "vehicle_mounted", "\"id\":\"%llu\"", (unsigned long long)c->net.id );
        return;
    }
    if ( state.tick_number - this->pending_mount_tick > 3 * static_cast<long>( UPS_RATE ) ) {
        mp_test::event( &state, "vehicle_mount_timeout", "\"id\":\"%llu\"", (unsigned long long)this->pending_mount_id );
        this->pending_mount_id = 0;
    }
}

// Dismount in multiplayer: stream an immediate parked state so viewers
// see the cart stop now (not 12 ticks later), then tell the server we're
// off. We keep rider_session — and keep simulating — until the server's
// COUNT=0 state lands; then sim_here falls back to the chunk owner and a
// ridden-in-foreign-chunk cart becomes a mirror again.
void Vehicles::local_dismount( RepGameState &state, Vehicle *cart ) {
    if ( this->net == nullptr || cart == nullptr || cart->net.id == 0 ) {
        return;
    }
    NetEntityStatePayload p;
    NetEntityStateEntry e;
    e.id = cart->net.id;
    e.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
    e.baseline = cart->net.sent_pos;
    e.present = NET_SE_POS_ABS | NET_SE_VEL | NET_SE_YAW;
    e.x = cart->pos.x;
    e.y = cart->pos.y;
    e.z = cart->pos.z;
    e.vx = static_cast<float>( cart->vel.x );
    e.vy = static_cast<float>( cart->vel.y );
    e.vz = static_cast<float>( cart->vel.z );
    e.yaw = cart->yaw;
    p.entries.push_back( e );
    this->net->send_state_now( p );
    const glm::dvec3 wire_vel = netsync_wire_vel( cart->vel );
    netsync_mark_sent( cart->net, cart->pos, wire_vel, state.tick_number, true );

    NetEntityActionPayload act = { };
    act.id = cart->net.id;
    act.action = NET_ACTION_DISMOUNT;
    this->net->send_entity_action( act );
    mp_test::event( &state, "vehicle_dismount_sent", "\"id\":\"%llu\"", (unsigned long long)cart->net.id );
}

// Connection dropped: clear the pending mount and get the local player
// out of the seat without wire traffic (sends are welcomed-gated no-ops
// anyway, but the intent is a purely local dismount).
void Vehicles::net_disconnect( RepGameState &state ) {
    this->pending_mount_id = 0;
    if ( state.camera.riding_vehicle != 0 ) {
        repgame_vehicle_dismount( state );
    }
}

int Vehicles::index_of( uint32_t id ) const {
    for ( size_t i = 0; i < this->vehicles.size( ); i++ ) {
        if ( this->vehicles[ i ].id == id ) {
            return static_cast<int>( i );
        }
    }
    return -1;
}

Vehicles::Vehicle *Vehicles::find( uint32_t id ) {
    const int i = this->index_of( id );
    return i < 0 ? nullptr : &this->vehicles[ i ];
}

// Same test-mode gate as the chunk's mobs/items loads: no cart file reads
// or writes under REPGAME_MOBS=0 / REPGAME_MOB_TEST.
static bool vehicle_persistence_enabled( ) {
    static const bool enabled = env_bool( "REPGAME_MOBS", true ) && !env_is_set( "REPGAME_MOB_TEST" );
    return enabled;
}

void Vehicles::adopt_chunk( Chunk &chunk ) {
    // Multiplayer mode: entity files live server-side; nothing local to load.
    if ( this->storage == nullptr || !vehicle_persistence_enabled( ) || this->mp_mode ) {
        chunk.sidecars.vehicles.clear( );
        return;
    }
    for ( const OwnedVehicleChunk &owned : this->owned_chunks ) {
        if ( owned.pos == chunk.chunk_pos ) {
            chunk.sidecars.vehicles.clear( );
            return;
        }
    }
    for ( const VehicleFileRecord &record : chunk.sidecars.vehicles ) {
        if ( !vehicle_type_valid( record.type ) ) {
            continue;
        }
        const uint32_t id = this->spawn( static_cast<VehicleType>( record.type ), glm::dvec3( record.x, record.y, record.z ), record.yaw );
        if ( id != 0 ) {
            this->vehicles.back( ).vel = glm::dvec3( record.vx, record.vy, record.vz );
        }
    }
    this->owned_chunks.push_back( { chunk.chunk_pos, chunk.sidecars.vehicles_file_present } );
    chunk.sidecars.vehicles.clear( );
}

void Vehicles::poll_owned( World &world ) {
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( size_t i = 0; i < this->owned_chunks.size( ); ) {
        const OwnedVehicleChunk &owned = this->owned_chunks[ i ];
        const Chunk *chunk = world.get_loaded_chunk( owned.pos * glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) );
        const bool left = chunk == nullptr || chunk->chunk_pos != owned.pos || chunk->load_state == CHUNK_PARKED;
        if ( !left ) {
            i++;
            continue;
        }
        std::vector<VehicleFileRecord> records;
        for ( size_t j = 0; j < this->vehicles.size( ); ) {
            Vehicle &c = this->vehicles[ j ];
            const glm::ivec3 owner = glm::ivec3( glm::floor( c.pos / csize ) );
            if ( c.persistent && owner == owned.pos ) {
                VehicleFileRecord record = { c.pos.x, c.pos.y, c.pos.z,
                                             static_cast<float>( c.vel.x ), static_cast<float>( c.vel.y ), static_cast<float>( c.vel.z ),
                                             c.yaw, c.type };
                records.push_back( record );
                remove_vehicle( j );
            } else {
                j++;
            }
        }
        // Rewrite (even empty) when there was a file so stale records don't
        // resurrect, or when there is something to record.
        if ( this->storage != nullptr && vehicle_persistence_enabled( ) && !RepGame::capture_mode( ) && !RepGame::fly_mode( ) && ( !records.empty( ) || owned.file_present ) ) {
            this->storage->persist_vehicles( owned.pos, records );
        }
        this->owned_chunks.erase( this->owned_chunks.begin( ) + static_cast<long>( i ) );
    }
}

void Vehicles::persist_all( ) {
    if ( this->storage == nullptr || !vehicle_persistence_enabled( ) || RepGame::capture_mode( ) || RepGame::fly_mode( ) || this->mp_mode ) {
        return;
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( const OwnedVehicleChunk &owned : this->owned_chunks ) {
        std::vector<VehicleFileRecord> records;
        for ( const Vehicle &c : this->vehicles ) {
            if ( c.persistent && glm::ivec3( glm::floor( c.pos / csize ) ) == owned.pos ) {
                VehicleFileRecord record = { c.pos.x, c.pos.y, c.pos.z,
                                             static_cast<float>( c.vel.x ), static_cast<float>( c.vel.y ), static_cast<float>( c.vel.z ),
                                             c.yaw, c.type };
                records.push_back( record );
            }
        }
        if ( !records.empty( ) || owned.file_present ) {
            this->storage->persist_vehicles( owned.pos, records );
        }
    }
}

// ---------------------------------------------------------------------------
// Multiplayer sync — same model as ItemEntities: the chunk owner's client
// simulates and streams ENTITY_STATE; all other viewers hold mirrors. The
// rider session id travels in the record/state `count` field and is
// server-authoritative (always 0 in phase 1); clients never emit
// NET_SE_COUNT on a VEHICLE entry.
// ---------------------------------------------------------------------------

void Vehicles::set_multiplayer_mode( const bool mp_mode, NetEntities *net ) {
    this->mp_mode = mp_mode;
    this->net = mp_mode ? net : nullptr;
}

Vehicles::Vehicle *Vehicles::find_by_net_id( const NetEntityId net_id ) {
    return netsync_find( this->vehicles, net_id );
}

const Vehicles::Vehicle *Vehicles::find_by_net_id( const NetEntityId net_id ) const {
    for ( const Vehicle &c : this->vehicles ) {
        if ( c.net.id == net_id && net_id != 0 ) {
            return &c;
        }
    }
    return nullptr;
}

// The player model's leg bones pivot at the hip, 12px above the feet.
static constexpr double PLAYER_LEG_PIVOT_HEIGHT = 12.0 / 16.0;

// mob.target is the avatar's FEET, so the seat pins them a leg-length
// below the def's seat_hip_height and seat_back behind centre (the
// vehicle's forward is (sin yaw, 0, -cos yaw)). The hip height puts the
// folded thighs on the interior floor so the eye lands just under the
// def's rider_eye_offset.
glm::dvec3 Vehicles::seat_pos( const Vehicle &c ) {
    const VehicleTypeDef &def = vehicle_type_def( c.type );
    const double ry = glm::radians( static_cast<double>( c.yaw ) );
    const glm::dvec3 fwd( sin( ry ), 0.0, -cos( ry ) );
    return c.pos - fwd * def.seat_back + glm::dvec3( 0.0, def.seat_hip_height - PLAYER_LEG_PIVOT_HEIGHT, 0.0 );
}

// The rider session (record `count`) overrides chunk ownership: the rider
// simulates the cart wherever it sits, everyone else mirrors.
bool Vehicles::sim_here( const Vehicle &c ) const {
    if ( this->net == nullptr ) {
        return !c.net.mirror; // single player / never connected
    }
    if ( c.rider_session != 0 ) {
        return c.rider_session == this->net->session( );
    }
    return this->net->owns_chunk( c.net.chunk );
}

// Store a record/state `count` (rider session id) and re-derive the
// mirror flag from it. Also resolves a pending MOUNT request: someone
// else riding means the server refused us.
void Vehicles::apply_rider( RepGameState * /*state*/, Vehicle &c, const int32_t count ) {
    c.rider_session = static_cast<uint32_t>( count );
    c.net.mirror = !this->sim_here( c );
    if ( c.net.id == this->pending_mount_id && c.rider_session != 0 && c.rider_session != ( this->net != nullptr ? this->net->session( ) : 0 ) ) {
        this->pending_mount_id = 0; // someone else got there first
    }
}

// Spawn one entity from a wire record (CHUNK_ROLE / ENTITY_SPAWN /
// ENTITY_TRANSFER). mirror=true means another client simulates it. A
// record carrying an invalid VehicleType is dropped (not spawned).
uint32_t Vehicles::spawn_record( const NetEntityRecord &rec, const bool mirror ) {
    if ( !vehicle_type_valid( rec.type ) ) {
        return 0;
    }
    const uint32_t id = this->spawn( static_cast<VehicleType>( rec.type ), glm::dvec3( rec.x, rec.y, rec.z ), rec.yaw, 1, true );
    if ( id == 0 ) {
        return 0;
    }
    Vehicle &c = this->vehicles.back( );
    c.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
    netsync_apply_record( c.net, rec, mirror );
    c.rider_session = static_cast<uint32_t>( rec.count );
    c.net.mirror = !this->sim_here( c );
    c.net_yaw = rec.yaw;
    c.net_flags = rec.flags;
    if ( mirror && ( rec.flags & NET_VEHICLE_FLAG_HURT ) ) {
        c.hurt_ticks = CART_HURT_WOBBLE_TICKS;
    }
    return id;
}

// ENTITY_SPAWN relay: pick the mirror flag ourselves — a rider field on
// the record beats the chunk-role guess.
void Vehicles::net_apply_spawn( const NetEntityRecord &rec ) {
    const glm::ivec3 rec_chunk = net_entity_chunk_of( glm::dvec3( rec.x, rec.y, rec.z ) );
    const uint32_t session = this->net != nullptr ? this->net->session( ) : 0;
    const bool mirror = rec.count != 0 ? static_cast<uint32_t>( rec.count ) != session
                                       : this->net == nullptr || !this->net->owns_chunk( rec_chunk );
    this->net_apply_spawn( rec, mirror );
}

void Vehicles::net_apply_spawn( const NetEntityRecord &rec, const bool mirror ) {
    Vehicle *existing = this->find_by_net_id( rec.id );
    if ( existing != nullptr ) {
        // Already local (e.g. our own predicted spawn): just set ownership.
        existing->net.mirror = mirror;
        existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
        existing->net.recv_pos = existing->net.pos;
        existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        existing->net.chunk = net_entity_chunk_of( existing->net.pos );
        this->apply_rider( nullptr, *existing, rec.count );
        return;
    }
    this->spawn_record( rec, mirror );
}

void Vehicles::net_apply_state( const NetEntityStateEntry &e ) {
    Vehicle *c = this->find_by_net_id( e.id );
    if ( c == nullptr ) {
        return;
    }
    // The incoming state reflects the server's belief: track the chunk it
    // places the entity in (same boundary hysteresis the server applies),
    // and only treat it as ours when we own that chunk.
    // The rider field is applied before the owned/mirror decision — it can
    // hand us (or take away) the sim with this very entry. A server-
    // originated COUNT state reaching the current sim only re-derives the
    // mirror flag; it never touches the owned pos.
    if ( e.present & NET_SE_COUNT ) {
        this->apply_rider( nullptr, *c, e.count );
    }
    const bool has_pos = ( e.present & ( NET_SE_POS_ABS | NET_SE_POS_DELTA ) ) != 0;
    if ( has_pos ) {
        c->net.recv_pos = glm::dvec3( e.x, e.y, e.z );
        netsync_track_chunk( c->net, c->net.recv_pos );
    }
    const bool owned_here = this->sim_here( *c );
    if ( c->net.mirror || !owned_here ) {
        // Mirror (or a predicted spawn we don't own): the streamed state is
        // authoritative — ease toward it.
        c->net.mirror = true;
        if ( has_pos ) {
            c->net.pos = c->net.recv_pos;
        }
        if ( e.present & NET_SE_VEL ) {
            c->net.vel = glm::dvec3( e.vx, e.vy, e.vz );
        }
        if ( e.present & NET_SE_YAW ) {
            c->net_yaw = e.yaw;
        }
        if ( e.present & NET_SE_FLAGS ) {
            if ( ( e.flags & NET_VEHICLE_FLAG_HURT ) && !( c->net_flags & NET_VEHICLE_FLAG_HURT ) ) {
                c->hurt_ticks = CART_HURT_WOBBLE_TICKS;
            }
            c->net_flags = e.flags;
        }
    }
}

void Vehicles::net_apply_remove( const NetEntityId net_id ) {
    for ( size_t i = 0; i < this->vehicles.size( ); i++ ) {
        if ( this->vehicles[ i ].net.id == net_id && net_id != 0 ) {
            remove_vehicle( i );
            return;
        }
    }
}

void Vehicles::net_release_entity( const NetEntityRecord &rec, const bool still_viewed ) {
    Vehicle *c = this->find_by_net_id( rec.id );
    if ( c == nullptr ) {
        return;
    }
    if ( still_viewed ) {
        c->net.mirror = true;
        c->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
        c->net.recv_pos = c->net.pos;
        c->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
        c->net.chunk = net_entity_chunk_of( c->net.pos );
        c->net_yaw = rec.yaw;
        c->net_flags = rec.flags;
        this->apply_rider( nullptr, *c, rec.count );
    } else {
        for ( size_t i = 0; i < this->vehicles.size( ); i++ ) {
            if ( &this->vehicles[ i ] == c ) {
                remove_vehicle( i );
                return;
            }
        }
    }
}

void Vehicles::net_apply_chunk_role( const glm::ivec3 &chunk, const uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids ) {
    if ( role == NET_ROLE_NONE ) {
        // Chunk left our view entirely: drop its synced entities.
        for ( size_t i = 0; i < this->vehicles.size( ); ) {
            const Vehicle &c = this->vehicles[ i ];
            if ( c.net.id != 0 && c.net.chunk == chunk ) {
                remove_vehicle( i );
            } else {
                i++;
            }
        }
        return;
    }
    const bool owned = role == NET_ROLE_OWNER;
    // The record list is authoritative: synced vehicles in this chunk that the
    // server doesn't know about are dropped.
    std::unordered_map<NetEntityId, const NetEntityRecord *> by_id;
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind == NetEntityKind::VEHICLE ) {
            by_id[ rec.id ] = &rec;
        }
    }
    const uint32_t session = this->net != nullptr ? this->net->session( ) : 0;
    for ( size_t i = 0; i < this->vehicles.size( ); ) {
        Vehicle &c = this->vehicles[ i ];
        if ( c.net.id != 0 && c.net.chunk == chunk && by_id.find( c.net.id ) == by_id.end( ) ) {
            // Our in-view spawns are always live server-side: absence from
            // the snapshot only means it predates the spawn, so keep them.
            // Stray spawns carry no id server-side — the wipe still applies
            // (a remapped record respawns in the same grant).
            const bool ours = session != 0 && ( c.net.id >> 32 ) == session && !this->net->stray_spawn( c.net.id );
            if ( !keep_ids.count( c.net.id ) && !ours ) {
                remove_vehicle( i );
                continue;
            }
            // Kept past a stale snapshot: settle sim ownership — a cart we
            // ride stays ours whatever the chunk role says.
            c.net.mirror = !this->sim_here( c );
        }
        i++;
    }
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind != NetEntityKind::VEHICLE ) {
            continue;
        }
        const bool rec_mirror = rec.count != 0 ? static_cast<uint32_t>( rec.count ) != session : !owned;
        Vehicle *existing = this->find_by_net_id( rec.id );
        if ( existing != nullptr ) {
            existing->net.mirror = rec_mirror;
            existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
            existing->net.recv_pos = existing->net.pos;
            existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
            existing->net_yaw = rec.yaw;
            existing->net_flags = rec.flags;
            // The grant's chunk is authoritative — a record resting in the
            // boundary margin has floor(pos) != the server's chunk.
            existing->net.chunk = chunk;
            this->apply_rider( nullptr, *existing, rec.count );
        } else {
            this->spawn_record( rec, rec_mirror );
            Vehicle *sp = this->find_by_net_id( rec.id );
            if ( sp != nullptr ) {
                sp->net.chunk = chunk;
            }
        }
    }
}

// ENTITY_ACTION forwarded by the server to us as the entity chunk's owner.
void Vehicles::net_apply_action( RepGameState &state, const NetEntityActionPayload &p ) {
    if ( p.action != NET_ACTION_HIT ) {
        return;
    }
    Vehicle *c = this->find_by_net_id( p.id );
    if ( c == nullptr || c->net.mirror || this->net == nullptr || !this->sim_here( *c ) ) {
        return;
    }
    const int index = static_cast<int>( c - this->vehicles.data( ) );
    const bool removed = this->hurt_local( state, index, p.attacker_survival != 0 );
    mp_test::event( &state, "cart_action_hit", "\"id\":\"%llu\",\"removed\":%d,\"survival\":%d",
                    (unsigned long long)p.id, (int)removed, (int)p.attacker_survival );
}

void Vehicles::collect_net_state( NetEntities &net, std::vector<NetEntityStateEntry> &out, const long tick_number ) {
    for ( Vehicle &c : this->vehicles ) {
        if ( c.net.id == 0 || c.net.mirror ) {
            continue;
        }
        // Ownership keys off the chunk the server believes the entity is
        // in (or the rider session when it's ridden): when it crosses into
        // a foreign chunk we keep streaming so the server sees the
        // crossing; a ridden cart keeps streaming until DISMOUNT lands.
        if ( !this->sim_here( c ) ) {
            continue;
        }
        const uint32_t flags = c.hurt_ticks > 0 ? NET_VEHICLE_FLAG_HURT : 0;
        const bool keyframe = !c.net.sent || netsync_keyframe_due( c.net, c.pos, tick_number );
        const bool crossing = c.net.sent && netsync_chunk_changed( c.net, c.pos );
        const bool yaw_changed = fabs( vehicle_wrap_180( c.yaw - c.net_sent_yaw ) ) > 0.5;
        const bool flags_changed = flags != c.net_sent_flags;
        // Dead reckoning: emit only when the sim diverges from what the
        // receiver predicts (or yaw/flags changed, a chunk boundary was
        // crossed, or the move-gated keyframe backstop is due).
        if ( !keyframe && !crossing && !yaw_changed && !flags_changed &&
             !netsync_drifted( c.net, c.pos, c.vel, tick_number ) && !netsync_stale( c.net, c.pos ) ) {
            continue;
        }
        NetEntityStateEntry e;
        e.id = c.net.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::VEHICLE );
        e.baseline = c.net.sent_pos;
        e.present = static_cast<uint8_t>( ( keyframe ? NET_SE_POS_ABS : NET_SE_POS_DELTA ) | NET_SE_VEL );
        const glm::dvec3 wire_pos = netsync_decoded_pos( c.net, c.pos, keyframe );
        e.x = wire_pos.x;
        e.y = wire_pos.y;
        e.z = wire_pos.z;
        const glm::dvec3 wire_vel = netsync_wire_vel( c.vel );
        e.vx = static_cast<float>( wire_vel.x );
        e.vy = static_cast<float>( wire_vel.y );
        e.vz = static_cast<float>( wire_vel.z );
        if ( keyframe || yaw_changed ) {
            e.present |= NET_SE_YAW;
            e.yaw = c.yaw;
        }
        if ( keyframe || flags_changed ) {
            e.present |= NET_SE_FLAGS;
            e.flags = flags;
        }
        // Never NET_SE_COUNT: the rider session id is server-authoritative.
        out.push_back( e );
        if ( e.present & NET_SE_YAW ) {
            c.net_sent_yaw = c.yaw;
        }
        if ( e.present & NET_SE_FLAGS ) {
            c.net_sent_flags = flags;
        }
        // The server now applies its boundary hysteresis to this pos.
        netsync_mark_sent( c.net, wire_pos, wire_vel, tick_number, keyframe );
    }
}

void Vehicles::drop_render_parts( Vehicle &c ) {
    if ( !this->render_inited ) {
        return;
    }
    for ( const entt::entity part : c.parts ) {
        this->render_chain.remove( part );
    }
    c.parts.clear( );
}

void Vehicles::update_render( float alpha, const ChunkLoader &loader ) {
    if ( !this->render_inited ) {
        return;
    }
    for ( Vehicle &c : this->vehicles ) {
        const VehicleTypeDef &def = vehicle_type_def( c.type );
        const MobModelDef &model = mob_models[ def.model ];
        glm::mat4 bone_world[ 32 ];
        // Boat paddles row with paddle_phase (advanced by horizontal speed
        // while ridden): a fore/aft sweep about the oarlock plus a small
        // blade lift on the return. The x-lift is per-side because the
        // bones' rest pitches are mirrored (positive anim raises the left
        // blade but lowers the right).
        glm::vec3 anim_rot[ 32 ];
        const bool has_paddles = def.physics == VehiclePhysics::WATER;
        if ( has_paddles ) {
            const float sweep = 28.0f * sinf( c.paddle_phase );
            const float lift = 10.0f * ( 0.5f + 0.5f * cosf( c.paddle_phase ) );
            for ( int i = 0; i < 32; i++ ) {
                anim_rot[ i ] = glm::vec3( 0.0f );
            }
            for ( int i = 0; i < model.bone_count && i < 32; i++ ) {
                const char *bn = model.bones[ i ].name;
                if ( strcmp( bn, "paddle_left" ) == 0 ) {
                    anim_rot[ i ].y = sweep;
                    anim_rot[ i ].x = -lift;
                } else if ( strcmp( bn, "paddle_right" ) == 0 ) {
                    anim_rot[ i ].y = sweep;
                    anim_rot[ i ].x = lift;
                }
            }
        }
        mob_bone_worlds( model, has_paddles ? anim_rot : nullptr, nullptr, bone_world );

        const glm::dvec3 rpos = glm::mix( c.prev_pos, c.pos, static_cast<double>( alpha ) );
        const float ryaw = c.prev_yaw + static_cast<float>( vehicle_wrap_180( c.yaw - c.prev_yaw ) ) * alpha;
        const float rpitch = c.prev_pitch + ( c.pitch - c.prev_pitch ) * alpha;

        // translate(lerp pos) · rotY(yaw) · rotX(pitch) · wobble roll ·
        // scale(model/16) · bone · cube — same base convention as mobs (model
        // -Z front lands on forward = (sin yaw, 0, -cos yaw)). The minecart
        // model's long axis is +x, so a -90° yaw pre-rot turns the cart
        // nose onto -Z.
        glm::mat4 base = glm::translate( glm::mat4( 1.0f ), glm::vec3( rpos ) );
        base = glm::rotate( base, glm::radians( -ryaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
        base = glm::rotate( base, glm::radians( rpitch ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
        // Hurt wobble: MC's hurtTime — a roll about the forward axis over
        // half a second, phase 1 rad per MC tick (20 tps), amplitude
        // decaying linearly from a 10° peak. The phase is interpolated by
        // render alpha so it doesn't step at the tick rate.
        if ( c.hurt_ticks > 0 ) {
            const float elapsed = static_cast<float>( CART_HURT_WOBBLE_TICKS - c.hurt_ticks ) + alpha;
            const float amp = 10.0f * static_cast<float>( c.hurt_ticks ) / CART_HURT_WOBBLE_TICKS;
            const float roll = amp * sinf( elapsed * ( 20.0f / UPS_RATE ) );
            base = glm::rotate( base, glm::radians( roll ), glm::vec3( 0.0f, 0.0f, 1.0f ) );
        }
        base = glm::rotate( base, glm::radians( -90.0f ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
        base = glm::scale( base, glm::vec3( def.model_scale / 16.0f ) );

        const glm::vec2 light = loader.light_sample( glm::vec3( rpos ) + glm::vec3( 0.0f, static_cast<float>( def.height ) * 0.5f, 0.0f ) );

        int part = 0;
        const int bone_count = model.bone_count < 32 ? model.bone_count : 32;
        for ( int i = 0; i < bone_count; i++ ) {
            const MobBoneDef &bone = model.bones[ i ];
            for ( int ci = 0; ci < bone.cube_count; ci++ ) {
                const MobCubeDef &cube = model.cubes[ bone.cube_start + ci ];
                const glm::mat4 cube_m = mob_cube_matrix( bone, cube );
                MobPartInstance &inst = this->render_chain.get_instance( c.parts[ part++ ] );
                inst.transform = base * bone_world[ i ] * cube_m;
                inst.light = light;
                inst.uv_a = glm::vec4( cube.uv[ 0 ], cube.uv[ 1 ], cube.size[ 0 ], cube.size[ 1 ] );
                inst.uv_b = glm::vec4( cube.size[ 2 ], static_cast<float>( def.skin ),
                                       ( bone.mirror || cube.mirror ) ? 1.0f : 0.0f, c.hurt_ticks > 0 ? -0.7f : 0.0f );
            }
        }
        while ( part < static_cast<int>( c.parts.size( ) ) ) {
            MobPartInstance &inst = this->render_chain.get_instance( c.parts[ part++ ] );
            inst.transform = glm::mat4( 0.0f );
        }
    }
    this->render_chain.invalidate_all( );
}

void Vehicles::draw( const Renderer &renderer, const Shader &shader ) {
    if ( !this->render_inited ) {
        return;
    }
    glDisable( GL_CULL_FACE );
    glEnable( GL_POLYGON_OFFSET_FILL );
    glPolygonOffset( -1.0f, -1.0f );
    this->render_chain.draw( renderer, shader );
    glPolygonOffset( 0.0f, 0.0f );
    glDisable( GL_POLYGON_OFFSET_FILL );
    glEnable( GL_CULL_FACE );
}

void Vehicles::cleanup( ) {
    for ( Vehicle &c : this->vehicles ) {
        this->drop_render_parts( c );
    }
    this->vehicles.clear( );
    this->owned_chunks.clear( );
}
