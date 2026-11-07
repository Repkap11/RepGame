#include "common/block_bodies.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "common/RepGame.hpp"
#include "common/block.hpp"
#include "common/chunk_loader.hpp"
#include "common/chunk.hpp"
#include "common/multiplayer.hpp"
#include "common/net_entities.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"

// Per-tick constants at UPS_RATE (144). Body gravity matches the old primed
// TNT's 9.8 blocks/s² — NOT the player's GRAVITY_STRENGTH (0.002/tick² ≈
// 41.5 blocks/s², which made falling blocks drop absurdly fast). Water
// gravity keeps the player's water/air ratio (0.0004/0.002 = 0.2). Terminal
// velocities still come from constants.hpp; drag is 0.98^(20/144); ground
// friction ~0.91^(20/144); water drag ~0.8^(20/144).
#define BODY_GRAVITY ( 9.8 / ( double( UPS_RATE ) * double( UPS_RATE ) ) )
#define BODY_WATER_GRAVITY ( BODY_GRAVITY * 0.2 )
#define BODY_AIR_DRAG 0.9972
#define BODY_WATER_DRAG 0.97
#define BODY_GROUND_FRICTION 0.952
#define BODY_WALL_RESTITUTION 0.2
// Horizontal speed under which a grounded body counts as still enough to
// settle; grid-centering pulls at most this far per tick.
#define BODY_SETTLE_SPEED 0.01
#define BODY_CENTER_PULL 0.02
// Ticks of grounded+slow+aligned on terrain before a falling block lands.
#define BODY_SETTLE_TICKS 4
// Grid-alignment tolerance for settling (position vs the centered cell).
#define BODY_ALIGN_EPS 0.05

void BlockBodies::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_block_object_instance, const VertexBufferLayout &vbl_primed_tnt_instance ) {
    this->render_falling.init( vbl_object_vertex, vbl_block_object_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_tnt.init( vbl_object_vertex, vbl_primed_tnt_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_inited = true;
}

static void resolve_body_faces( const BlockState &state, unsigned short face[ NUM_FACES_IN_CUBE ] ) {
    // Resolve the displayed textures the same way chunk meshing does: per-face
    // lookup, then state-based adjustment.
    const Block *block = block_definition_get_definition( state.id );
    BlockCoords coords = {};
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        coords.face[ f ] = block->textures[ f ];
    }
    block_adjust_coord_based_on_state( block, &state, &coords );
    for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
        face[ f ] = coords.face[ f ];
    }
}

// The cell a resting body's box snaps to: each axis rounds the box min.
static glm::ivec3 rest_cell( const glm::dvec3 &min ) {
    return glm::ivec3( glm::floor( min + glm::dvec3( 0.5 ) ) );
}

static glm::dvec3 body_center( const BlockBodies::Body &b ) {
    return b.min + glm::dvec3( BlockBodies::BODY_SIZE * 0.5 );
}

bool BlockBodies::body_chunk_live( World &world, const Body &b ) const {
    const Chunk *chunk = world.get_loaded_chunk( glm::ivec3( glm::floor( body_center( b ) ) ) );
    return chunk != nullptr && chunk->load_state == CHUNK_LIVE;
}

void BlockBodies::remove_body( const size_t i ) {
    Body &b = this->bodies[ i ];
    if ( b.entity != entt::null ) {
        if ( b.kind == BODY_FALLING ) {
            this->render_falling.remove( b.entity );
        } else {
            this->render_tnt.remove( b.entity );
        }
    }
    this->bodies.erase( this->bodies.begin( ) + i );
}

// Writes the body back into the world at its rest cell when the cell can take
// it; breaks it into debris when the cell holds a non-colliding,
// non-replaceable block. Returns true when the body is consumed (landed or
// broken), false when it must keep waiting (cell blocked by the player).
bool BlockBodies::settle_land( RepGameState &state, Body &b ) {
    World &world = state.world;
    glm::ivec3 cell = rest_cell( b.min );
    BlockState cur = world.get_loaded_block( cell );
    if ( cur.id == LAST_BLOCK_ID ) {
        return false; // unloaded — wait (the live check should prevent this)
    }
    const Block *cur_block = block_definition_get_definition( cur.id );
    // The rest cell went solid after the body got there — another body landed
    // underneath it. Stack on top of the new column instead of clobbering.
    while ( cur_block->collides_with_player ) {
        cell.y++;
        cur = world.get_loaded_block( cell );
        if ( cur.id == LAST_BLOCK_ID ) {
            return false;
        }
        cur_block = block_definition_get_definition( cur.id );
    }
    // Resting on the world edge (the cell below is unloaded space): the block
    // can't be written through the halo fixup, so it drops as debris — same
    // fate as landing on a torch cell.
    if ( world.get_loaded_block( cell + glm::ivec3( 0, -1, 0 ) ).id == LAST_BLOCK_ID ) {
        world.spawn_block_debris( cell, b.state, AIR );
        return true;
    }
    if ( cur.id == AIR || cur.id == WATER || cur_block->can_be_placed_in ) {
        // The cell overlaps the player: wait rather than place a block inside
        // them.
        if ( Collision::check_collides_with_block( world, state.camera.pos, cell, state.camera.player_height, state.camera.eye_height ) ) {
            return false;
        }
        if ( cur.id != AIR && cur.id != WATER ) {
            // Replacing a plant/cutout block: it breaks with debris.
            world.spawn_block_debris( cell, cur, b.state.id );
        }
        world.set_loaded_block( cell, b.state );
        state.multiplayer.set_block( cell, b.state );
        queue_neighbor_block_updates( state.blockUpdateQueue, world, state.tick_number, cell );
        lift_bodies_above( cell, b.id );
        return true;
    }
    if ( !cur_block->collides_with_player ) {
        // Some other non-colliding block (torch cell, piston head, ...) — the
        // falling block can't occupy it and breaks into debris.
        world.spawn_block_debris( cell, b.state, AIR );
        return true;
    }
    // A solid block at the rest cell shouldn't happen — the body was resting
    // on top of it — but never clobber it.
    return false;
}

void BlockBodies::lift_bodies_above( const glm::ivec3 &cell, const uint32_t skip_id ) {
    struct Pusher {
        double xmin, xmax, zmin, zmax, top, center_y;
    };
    std::vector<Pusher> pushers;
    pushers.push_back( { static_cast<double>( cell.x ), cell.x + 1.0, static_cast<double>( cell.z ), cell.z + 1.0, cell.y + 1.0,
                         cell.y + 0.5 } );
    std::vector<size_t> order( this->bodies.size( ) );
    for ( size_t i = 0; i < order.size( ); i++ ) {
        order[ i ] = i;
    }
    std::sort( order.begin( ), order.end( ), [ this ]( size_t a, size_t b ) { return this->bodies[ a ].min.y < this->bodies[ b ].min.y; } );
    for ( const size_t oi : order ) {
        Body &b = this->bodies[ oi ];
        if ( b.id == skip_id || b.net.mirror ) {
            continue;
        }
        const double bcy = b.min.y + BODY_SIZE * 0.5;
        double lift_top = -1e30;
        for ( const Pusher &p : pushers ) {
            // Strict XZ footprint overlap, bottom embedded below the pusher's
            // top, center above the pusher's center (resting on/embedded from
            // above — not beside or below).
            if ( b.min.x + BODY_SIZE <= p.xmin || b.min.x >= p.xmax || b.min.z + BODY_SIZE <= p.zmin || b.min.z >= p.zmax ) {
                continue;
            }
            if ( b.min.y >= p.top || bcy <= p.center_y ) {
                continue;
            }
            if ( p.top > lift_top ) {
                lift_top = p.top;
            }
        }
        if ( lift_top == -1e30 ) {
            continue;
        }
        const double dy = lift_top + 1e-4 - b.min.y;
        b.min.y += dy;
        b.prev_min.y += dy; // don't let render interpolation show the lift
        if ( b.vel.y < 0.0 ) {
            b.vel.y = 0.0;
        }
        pushers.push_back( { b.min.x, b.min.x + BODY_SIZE, b.min.z, b.min.z + BODY_SIZE, b.min.y + BODY_SIZE,
                             b.min.y + BODY_SIZE * 0.5 } );
    }
}

uint32_t BlockBodies::spawn_falling( const glm::ivec3 &cell, const BlockState &state, const glm::dvec3 &vel ) {
    if ( this->bodies.size( ) >= MAX_BLOCK_BODIES ) {
        return 0; // at the cap: the block stays in the world
    }
    Body b;
    b.id = this->next_id++;
    if ( this->next_id == 0 ) {
        this->next_id = 1;
    }
    b.kind = BODY_FALLING;
    b.state = state;
    b.mass = block_definition_get_definition( state.id )->fall_mass;
    const double inset = ( 1.0 - BODY_SIZE ) * 0.5;
    b.min = glm::dvec3( cell ) + glm::dvec3( inset );
    b.prev_min = b.min;
    b.vel = vel;
    resolve_body_faces( state, b.face );
    this->bodies.push_back( b );
    // Multiplayer: announce the spawn (mints a net id). No-op unbound or
    // before WELCOME — those bodies stay local-only.
    this->announce_spawn( this->bodies.back( ) );
    return b.id;
}

uint32_t BlockBodies::spawn_primed_tnt( RepGameState &state, const glm::dvec3 &center, const glm::dvec3 &vel, const BlockID id, const long fuse_ticks, const long now_tick ) {
    if ( this->bodies.size( ) >= MAX_BLOCK_BODIES ) {
        // A fuse must never go missing: evict the oldest falling body, which
        // lands where it is if it can and otherwise breaks into debris.
        for ( size_t i = 0; i < this->bodies.size( ); i++ ) {
            Body &b = this->bodies[ i ];
            if ( b.kind != BODY_FALLING ) {
                continue;
            }
            if ( settle_land( state, b ) ) {
                this->send_net_remove( b, NET_REMOVE_LANDED );
                if ( this->render_inited ) {
                    Landed l;
                    l.min = b.min;
                    l.cell = rest_cell( b.min );
                    l.id = b.state.id;
                    l.entity = b.entity;
                    memcpy( l.face, b.face, sizeof( l.face ) );
                    b.entity = entt::null;
                    this->landed.push_back( l );
                }
            } else {
                this->send_net_remove( b, NET_REMOVE_DESPAWN );
                state.world.spawn_block_debris( glm::ivec3( glm::floor( body_center( b ) ) ), b.state, AIR );
                if ( b.entity != entt::null ) {
                    this->render_falling.remove( b.entity );
                    b.entity = entt::null;
                }
            }
            this->bodies.erase( this->bodies.begin( ) + i );
            break;
        }
    }
    Body b;
    b.id = this->next_id++;
    if ( this->next_id == 0 ) {
        this->next_id = 1;
    }
    b.kind = BODY_PRIMED_TNT;
    b.state = { id, 0, 0, id, 0 };
    b.mass = 1.0f;
    b.min = center - glm::dvec3( BODY_SIZE * 0.5 );
    b.prev_min = b.min;
    b.vel = vel;
    b.fuse_start_tick = now_tick;
    b.fuse_ticks = fuse_ticks;
    resolve_body_faces( b.state, b.face );
    this->bodies.push_back( b );
    this->announce_spawn( this->bodies.back( ) );
    return b.id;
}

bool BlockBodies::take( const uint32_t id, glm::dvec3 *center ) {
    if ( id == 0 ) {
        return false;
    }
    for ( size_t i = 0; i < this->bodies.size( ); i++ ) {
        if ( this->bodies[ i ].id == id ) {
            if ( center != nullptr ) {
                *center = body_center( this->bodies[ i ] );
            }
            // The fuse's explosion consumed it — mirrors see EXPLODED.
            this->send_net_remove( this->bodies[ i ], NET_REMOVE_EXPLODED );
            remove_body( i );
            return true;
        }
    }
    return false;
}

void BlockBodies::impulse( const glm::dvec3 &center, const double radius, const double strength ) {
    for ( Body &b : this->bodies ) {
        if ( b.net.mirror ) {
            continue; // mirrors are shoved by their own sim's blast
        }
        const glm::dvec3 offset = body_center( b ) - center;
        const double d = glm::length( offset );
        if ( d >= radius ) {
            continue;
        }
        // Straight up when the body sits on the blast center.
        const glm::dvec3 dir = d > 1e-6 ? offset / d : glm::dvec3( 0.0, 1.0, 0.0 );
        b.vel += dir * strength * ( 1.0 - d / radius ) / static_cast<double>( b.mass );
    }
}

void BlockBodies::land_all( RepGameState &state ) {
    World &world = state.world;
    for ( Body &b : this->bodies ) {
        if ( b.net.mirror ) {
            continue; // mirrors land on their own sim's client
        }
        this->send_net_remove( b, NET_REMOVE_DESPAWN );
        const glm::dvec3 c = body_center( b );
        const glm::ivec3 cell = b.kind == BODY_FALLING ? rest_cell( b.min )
                                                     : glm::ivec3( glm::floor( c ) );
        const BlockState cur = world.get_loaded_block( cell );
        if ( cur.id == LAST_BLOCK_ID ) {
            continue; // unloaded — dropped rather than stalling shutdown
        }
        const Block *cur_block = block_definition_get_definition( cur.id );
        if ( cur.id == AIR || cur.id == WATER || cur_block->can_be_placed_in ) {
            world.set_loaded_block( cell, b.state );
        }
    }
    cleanup( );
}

// Cell-keyed spatial hash key for the body-collision broadphase.
static int64_t cell_key( const glm::ivec3 &c ) {
    return ( ( static_cast<int64_t>( c.x ) & 0x1FFFFF ) << 42 ) | ( ( static_cast<int64_t>( c.y ) & 0x1FFFFF ) << 21 ) | ( static_cast<int64_t>( c.z ) & 0x1FFFFF );
}

void BlockBodies::tick( RepGameState &state ) {
    World &world = state.world;
    this->now_tick = state.tick_number;

    // Frozen in capture mode, and frozen per body while the chunk at its
    // center is not CHUNK_LIVE (the mobs freeze pattern).
    const bool freeze = RepGame::capture_mode( );

    // Broadphase: a cell-keyed hash over the cells each body box overlaps.
    std::unordered_map<int64_t, std::vector<int>> hash;
    hash.reserve( this->bodies.size( ) * 4 );
    for ( size_t i = 0; i < this->bodies.size( ); i++ ) {
        if ( this->bodies[ i ].net.mirror ) {
            continue; // mirrors never collide with simulated bodies
        }
        const glm::dvec3 &mn = this->bodies[ i ].min;
        const glm::dvec3 mx = mn + glm::dvec3( BODY_SIZE );
        const glm::ivec3 c0( glm::floor( mn ) );
        const glm::ivec3 c1( glm::floor( mx - glm::dvec3( 1e-4 ) ) );
        for ( int x = c0.x; x <= c1.x; x++ ) {
            for ( int y = c0.y; y <= c1.y; y++ ) {
                for ( int z = c0.z; z <= c1.z; z++ ) {
                    hash[ cell_key( glm::ivec3( x, y, z ) ) ].push_back( static_cast<int>( i ) );
                }
            }
        }
    }

    // Bottom-up processing so a lower body's new position is what an upper
    // body collides with this tick.
    std::vector<size_t> order( this->bodies.size( ) );
    for ( size_t i = 0; i < order.size( ); i++ ) {
        order[ i ] = i;
    }
    std::sort( order.begin( ), order.end( ), [ this ]( size_t a, size_t b ) { return this->bodies[ a ].min.y < this->bodies[ b ].min.y; } );

    std::vector<BodyBox> extra;
    std::vector<size_t> landed_idx;

    for ( const size_t oi : order ) {
        Body &b = this->bodies[ oi ];
        b.prev_min = b.min;
        if ( b.net.mirror ) {
            // Mirror of a remote-simulated body: interpolation only — no
            // physics, settling, impulses or collisions. ENTITY_REMOVE (or
            // CHUNK_ROLE NONE / disconnect) ends it.
            const glm::dvec3 delta = b.net.pos - b.min;
            if ( glm::length( delta ) > 4.0 ) {
                b.min = b.net.pos;
            } else {
                b.min += delta * 0.35;
            }
            b.vel = b.net.vel;
            continue;
        }
        if ( freeze || !body_chunk_live( world, b ) ) {
            continue;
        }

        // Water: the cell at the body center is water or waterlogged.
        const glm::dvec3 center = body_center( b );
        const BlockState medium = world.get_loaded_block( glm::ivec3( glm::floor( center ) ) );
        const bool in_water = medium.id == WATER || medium.waterlogged;

        const double gravity = in_water ? BODY_WATER_GRAVITY : BODY_GRAVITY;
        const double drag = in_water ? BODY_WATER_DRAG : BODY_AIR_DRAG;
        const double terminal = in_water ? SWIM_TERMINAL_VELOCITY : TERMINAL_VELOCITY;

        b.vel.y -= gravity;
        b.vel *= drag;
        if ( b.vel.y > terminal ) {
            b.vel.y = terminal;
        } else if ( b.vel.y < -terminal ) {
            b.vel.y = -terminal;
        }

        // Nearby-body candidates: every body registered in a cell of this
        // body's box expanded past one tick of travel + one body width.
        extra.clear( );
        const double reach = TERMINAL_VELOCITY + BODY_SIZE;
        const glm::dvec3 q0( glm::floor( b.min - glm::dvec3( reach ) ) );
        const glm::dvec3 q1( glm::floor( b.min + glm::dvec3( BODY_SIZE + reach ) ) );
        for ( int x = static_cast<int>( q0.x ); x <= static_cast<int>( q1.x ); x++ ) {
            for ( int y = static_cast<int>( q0.y ); y <= static_cast<int>( q1.y ); y++ ) {
                for ( int z = static_cast<int>( q0.z ); z <= static_cast<int>( q1.z ); z++ ) {
                    const auto it = hash.find( cell_key( glm::ivec3( x, y, z ) ) );
                    if ( it == hash.end( ) ) {
                        continue;
                    }
                    for ( const int j : it->second ) {
                        if ( static_cast<size_t>( j ) == oi ) {
                            continue;
                        }
                        bool seen = false;
                        for ( const BodyBox &bb : extra ) {
                            if ( bb.index == j ) {
                                seen = true;
                                break;
                            }
                        }
                        if ( !seen ) {
                            const glm::dvec3 &mn = this->bodies[ j ].min;
                            extra.push_back( { mn, mn + glm::dvec3( BODY_SIZE ), j } );
                        }
                    }
                }
            }
        }

        bool hit_axis[ 3 ];
        int hit_extra_axis[ 3 ];
        bool standing = false;
        glm::dvec3 max = b.min + glm::dvec3( BODY_SIZE );
        Collision::move_box( world, b.min, max, b.vel, extra, hit_axis, hit_extra_axis, &standing );

        for ( int axis = 0; axis < 3; axis++ ) {
            if ( !hit_axis[ axis ] ) {
                continue;
            }
            if ( hit_extra_axis[ axis ] >= 0 ) {
                // Mass-weighted inelastic exchange: both bodies take the
                // shared velocity on this axis.
                Body &other = this->bodies[ hit_extra_axis[ axis ] ];
                const double shared = ( b.mass * b.vel[ axis ] + other.mass * other.vel[ axis ] ) / ( b.mass + other.mass );
                b.vel[ axis ] = shared;
                other.vel[ axis ] = shared;
            } else if ( axis == 1 ) {
                b.vel.y = 0.0;
            } else {
                b.vel[ axis ] = -b.vel[ axis ] * BODY_WALL_RESTITUTION;
            }
        }
        b.grounded = standing ? 1 : 0;
        if ( b.grounded ) {
            b.vel.x *= BODY_GROUND_FRICTION;
            b.vel.z *= BODY_GROUND_FRICTION;
        }

        if ( b.kind != BODY_FALLING ) {
            continue;
        }

        // Grid alignment: while grounded and slow, pull toward the box
        // centered on the cell under the body center. When that cell has no
        // support below, the pull keeps going — the body slides off the
        // ledge until nothing holds it, then falls.
        const glm::ivec3 center_cell( glm::floor( center ) );
        const glm::dvec2 target( center_cell.x + ( 1.0 - BODY_SIZE ) * 0.5, center_cell.z + ( 1.0 - BODY_SIZE ) * 0.5 );
        const bool slow = std::fabs( b.vel.x ) < BODY_SETTLE_SPEED && std::fabs( b.vel.z ) < BODY_SETTLE_SPEED;
        if ( b.grounded && slow ) {
            for ( int axis = 0; axis < 2; axis++ ) {
                const double off = target[ axis ] - b.min[ axis == 0 ? 0 : 2 ];
                if ( std::fabs( off ) <= BODY_CENTER_PULL ) {
                    b.min[ axis == 0 ? 0 : 2 ] = target[ axis ];
                } else {
                    b.min[ axis == 0 ? 0 : 2 ] += std::copysign( BODY_CENTER_PULL, off );
                }
            }
        }
        // Unloaded space below still supports a resting body — it settled on
        // the world edge; settle_land turns that into debris.
        const BlockState below = world.get_loaded_block( center_cell + glm::ivec3( 0, -1, 0 ) );
        const bool supported = below.id == LAST_BLOCK_ID || block_definition_get_definition( below.id )->collides_with_player;
        const bool aligned = std::fabs( b.min.x - target.x ) < BODY_ALIGN_EPS && std::fabs( b.min.z - target.y ) < BODY_ALIGN_EPS &&
                             std::fabs( b.min.y - glm::round( b.min.y ) ) < BODY_ALIGN_EPS;
        if ( b.grounded && slow && aligned && supported && hit_extra_axis[ 1 ] < 0 ) {
            if ( ++b.settle_ticks >= BODY_SETTLE_TICKS && settle_land( state, b ) ) {
                this->send_net_remove( b, NET_REMOVE_LANDED );
                landed_idx.push_back( oi );
            }
        } else {
            b.settle_ticks = 0;
        }
    }

    // Consume landed bodies (descending index order keeps indices valid).
    std::sort( landed_idx.rbegin( ), landed_idx.rend( ) );
    for ( const size_t i : landed_idx ) {
        Body &b = this->bodies[ i ];
        if ( this->render_inited ) {
            Landed l;
            l.min = b.min;
            l.cell = rest_cell( b.min );
            l.id = b.state.id;
            l.entity = b.entity;
            memcpy( l.face, b.face, sizeof( l.face ) );
            b.entity = entt::null;
            this->landed.push_back( l );
        }
        remove_body( i );
    }
}

void BlockBodies::update_render( const float alpha, const ChunkLoader &chunk_loader ) {
    if ( !this->render_inited ) {
        return;
    }
    const glm::vec3 scale_v( static_cast<float>( BODY_SIZE ) );
    for ( Body &b : this->bodies ) {
        if ( b.entity == entt::null ) {
            if ( b.kind == BODY_FALLING ) {
                const std::pair<entt::entity, BlockObjectInstance &> data = this->render_falling.create_instance( );
                b.entity = data.first;
                for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                    data.second.face[ f ] = b.face[ f ];
                }
                // The block's own shape, literal offset: the drawn body must
                // match the block it lands as.
                block_object_shape( block_definition_get_definition( b.state.id ), false, data.second.shape_scale, data.second.shape_offset, data.second.shape_tex_offset );
            } else {
                const std::pair<entt::entity, PrimedTntInstance &> data = this->render_tnt.create_instance( );
                b.entity = data.first;
                for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                    data.second.face[ f ] = b.face[ f ];
                }
            }
        }
        const glm::dvec3 rmin = glm::mix( b.prev_min, b.min, static_cast<double>( alpha ) );
        const glm::mat4 transform = glm::scale( glm::translate( glm::mat4( 1.0f ), glm::vec3( rmin ) ), scale_v );
        const glm::vec2 light = chunk_loader.light_sample( glm::vec3( body_center( b ) ) );
        if ( b.kind == BODY_FALLING ) {
            BlockObjectInstance &inst = this->render_falling.get_instance( b.entity );
            inst.transform = transform;
            inst.light = light;
        } else {
            PrimedTntInstance &inst = this->render_tnt.get_instance( b.entity );
            inst.transform = transform;
            inst.light = light;
            const double age_s = static_cast<double>( now_tick - b.fuse_start_tick ) / UPS_RATE;
            inst.flash = static_cast<float>( static_cast<long>( floor( age_s * 4.0 ) ) % 2 );
        }
    }

    // Landed bodies keep drawing at their rest cell until the target chunk's
    // remesh caught up (repop_must budget overflow is the only reason a
    // landed write could still be pending a frame later).
    for ( size_t i = 0; i < this->landed.size( ); ) {
        Landed &l = this->landed[ i ];
        const glm::vec3 chunk_size( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
        const Chunk *chunk = chunk_loader.get_chunk( glm::ivec3( glm::floor( glm::vec3( l.cell ) / chunk_size ) ) );
        if ( chunk == nullptr || chunk->needs_repopulation == 0 ) {
            if ( l.entity != entt::null ) {
                this->render_falling.remove( l.entity );
            }
            this->landed[ i ] = this->landed.back( );
            this->landed.pop_back( );
            continue;
        }
        if ( l.entity == entt::null ) {
            const std::pair<entt::entity, BlockObjectInstance &> data = this->render_falling.create_instance( );
            l.entity = data.first;
            for ( int f = 0; f < NUM_FACES_IN_CUBE; f++ ) {
                data.second.face[ f ] = l.face[ f ];
            }
            block_object_shape( block_definition_get_definition( l.id ), false, data.second.shape_scale, data.second.shape_offset, data.second.shape_tex_offset );
        }
        BlockObjectInstance &inst = this->render_falling.get_instance( l.entity );
        inst.transform = glm::scale( glm::translate( glm::mat4( 1.0f ), glm::vec3( l.min ) ), scale_v );
        inst.light = chunk_loader.light_sample( glm::vec3( l.cell ) + glm::vec3( 0.5f ) );
        i++;
    }

    this->render_falling.invalidate_all( );
    this->render_tnt.invalidate_all( );
}

void BlockBodies::draw( const Renderer &renderer, const Shader &block_object_shader, const Shader &primed_tnt_shader ) {
    if ( !this->render_inited ) {
        return;
    }
    this->render_falling.draw( renderer, block_object_shader );
    this->render_tnt.draw( renderer, primed_tnt_shader );
}

size_t BlockBodies::count_falling( ) const {
    size_t n = 0;
    for ( const Body &b : this->bodies ) {
        n += b.kind == BODY_FALLING;
    }
    return n;
}

size_t BlockBodies::count_tnt( ) const {
    size_t n = 0;
    for ( const Body &b : this->bodies ) {
        n += b.kind == BODY_PRIMED_TNT;
    }
    return n;
}

void BlockBodies::cleanup( ) {
    this->render_falling.clear( );
    this->render_tnt.clear( );
    this->bodies.clear( );
    this->landed.clear( );
}

// ---------------------------------------------------------------------------
// Multiplayer sync ("causer simulates", not chunk-owner simulates). The
// spawning client streams ENTITY_STATE for bodies it created; mirrors just
// interpolate. All paths are no-ops in single player.
// ---------------------------------------------------------------------------

static glm::ivec3 body_net_chunk_of( const glm::dvec3 &pos ) {
    return net_entity_chunk_of( pos );
}

void BlockBodies::set_multiplayer_mode( const bool mp_mode, NetEntities *net ) {
    this->mp_mode = mp_mode;
    this->net = mp_mode ? net : nullptr;
}

void BlockBodies::announce_spawn( Body &b ) {
    if ( this->net == nullptr || b.net.id != 0 ) {
        return;
    }
    const NetEntityId nid = this->net->mint_id( );
    if ( nid == 0 ) {
        return;
    }
    b.net.id = nid;
    b.net.chunk = body_net_chunk_of( b.min );
    NetEntityRecord rec;
    rec.id = nid;
    rec.kind = NetEntityKind::BODY;
    rec.x = b.min.x;
    rec.y = b.min.y;
    rec.z = b.min.z;
    rec.vx = static_cast<float>( b.vel.x );
    rec.vy = static_cast<float>( b.vel.y );
    rec.vz = static_cast<float>( b.vel.z );
    rec.type = static_cast<uint32_t>( b.kind );
    rec.block_state = b.state;
    rec.fuse_ticks = static_cast<int32_t>( std::max<long>( 0, b.fuse_ticks - ( this->now_tick - b.fuse_start_tick ) ) );
    this->net->send_spawn( rec );
}

void BlockBodies::send_net_remove( Body &b, const uint8_t reason ) {
    netsync_send_remove( this->net, b.net, reason );
}

BlockBodies::Body *BlockBodies::find_by_net_id( const NetEntityId net_id ) {
    return netsync_find( this->bodies, net_id );
}

bool BlockBodies::spawn_record( const NetEntityRecord &rec, const bool mirror, const long now_tick ) {
    if ( this->bodies.size( ) >= MAX_BLOCK_BODIES || rec.type > BODY_PRIMED_TNT ) {
        return false;
    }
    Body b;
    b.id = this->next_id++;
    if ( this->next_id == 0 ) {
        this->next_id = 1;
    }
    b.kind = static_cast<BodyKind>( rec.type );
    b.state = rec.block_state;
    b.mass = b.kind == BODY_FALLING ? block_definition_get_definition( b.state.id )->fall_mass : 1.0f;
    b.min = glm::dvec3( rec.x, rec.y, rec.z );
    b.prev_min = b.min;
    b.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
    if ( b.kind == BODY_PRIMED_TNT ) {
        // Mirrors blink from their own clock with the stream's remaining
        // fuse; adopted bodies keep the wire fuse for the explode event.
        b.fuse_start_tick = now_tick;
        b.fuse_ticks = rec.fuse_ticks;
    }
    resolve_body_faces( b.state, b.face );
    netsync_apply_record( b.net, rec, mirror );
    this->bodies.push_back( b );
    return true;
}

void BlockBodies::net_apply_spawn( const NetEntityRecord &rec, const bool mirror, const long now_tick ) {
    Body *existing = this->find_by_net_id( rec.id );
    if ( existing != nullptr ) {
        if ( existing->net.mirror ) {
            existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
            existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
            existing->net.chunk = body_net_chunk_of( existing->net.pos );
        }
        return;
    }
    this->spawn_record( rec, mirror, now_tick );
}

void BlockBodies::net_apply_state( const NetEntityStateEntry &e ) {
    Body *b = this->find_by_net_id( e.id );
    if ( b == nullptr || !b->net.mirror ) {
        return;
    }
    b->net.pos = glm::dvec3( e.x, e.y, e.z );
    b->net.vel = glm::dvec3( e.vx, e.vy, e.vz );
    netsync_track_chunk( b->net, b->net.pos );
}

void BlockBodies::net_apply_remove( const NetEntityId net_id ) {
    for ( size_t i = 0; i < this->bodies.size( ); i++ ) {
        if ( this->bodies[ i ].net.id == net_id && net_id != 0 ) {
            remove_body( i );
            return;
        }
    }
}

void BlockBodies::net_apply_transfer( RepGameState &state, const NetEntityRecord &rec ) {
    // Sim handover after the previous simulating client disconnected: we now
    // own the body. Mirrors with this id become owned; missing bodies spawn.
    Body *b = this->find_by_net_id( rec.id );
    if ( b == nullptr ) {
        if ( !this->spawn_record( rec, false, state.tick_number ) ) {
            return;
        }
        b = &this->bodies.back( );
    }
    b->net.mirror = false;
    b->net.sent = false; // stream resumes with the next collect
    if ( b->kind == BODY_PRIMED_TNT ) {
        // The previous sim's explode event died with it — reschedule from
        // the wire record's remaining fuse, same shape as tnt_launch's.
        const bool inverse = block_definition_get_definition( b->state.id )->is_inverse_tnt;
        const long remaining = std::max<long>( 1, b->fuse_ticks );
        const glm::ivec3 explode_pos = glm::ivec3( glm::floor( body_center( *b ) ) );
        state.blockUpdateQueue.addBlockUpdate( std::make_shared<TntExplodeEvent>( state.tick_number + remaining, explode_pos, b->id, inverse ) );
    }
}

void BlockBodies::net_apply_chunk_role( const glm::ivec3 &chunk, const uint8_t role, const std::vector<NetEntityRecord> &records, const std::set<NetEntityId> &keep_ids ) {
    if ( role == NET_ROLE_NONE ) {
        for ( size_t i = 0; i < this->bodies.size( ); ) {
            if ( this->bodies[ i ].net.mirror && this->bodies[ i ].net.chunk == chunk ) {
                remove_body( i );
            } else {
                i++;
            }
        }
        return;
    }
    // VIEWER/OWNER: sync mirror bodies to the record list. Roles don't move
    // body ownership — bodies we already simulate are left alone.
    std::unordered_map<NetEntityId, const NetEntityRecord *> by_id;
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind == NetEntityKind::BODY ) {
            by_id[ rec.id ] = &rec;
        }
    }
    for ( size_t i = 0; i < this->bodies.size( ); ) {
        const Body &b = this->bodies[ i ];
        if ( b.net.mirror && b.net.id != 0 && b.net.chunk == chunk && by_id.find( b.net.id ) == by_id.end( ) && !keep_ids.count( b.net.id ) ) {
            remove_body( i );
        } else {
            i++;
        }
    }
    for ( const NetEntityRecord &rec : records ) {
        if ( rec.kind != NetEntityKind::BODY ) {
            continue;
        }
        Body *existing = this->find_by_net_id( rec.id );
        if ( existing != nullptr ) {
            if ( existing->net.mirror ) {
                existing->net.pos = glm::dvec3( rec.x, rec.y, rec.z );
                existing->net.vel = glm::dvec3( rec.vx, rec.vy, rec.vz );
                // The grant's chunk is authoritative (margin records have
                // floor(pos) != the server's chunk).
                existing->net.chunk = chunk;
            }
        } else if ( this->spawn_record( rec, true, this->now_tick ) ) {
            this->bodies.back( ).net.chunk = chunk;
        }
    }
}

void BlockBodies::net_disconnect( ) {
    // Mirror bodies freeze mid-air without their sim — drop them. Bodies we
    // simulate keep flying locally.
    for ( size_t i = 0; i < this->bodies.size( ); ) {
        if ( this->bodies[ i ].net.mirror ) {
            remove_body( i );
        } else {
            i++;
        }
    }
}

void BlockBodies::collect_net_state( std::vector<NetEntityStateEntry> &out, const long tick_number ) {
    for ( Body &b : this->bodies ) {
        if ( b.net.id == 0 || b.net.mirror ) {
            continue;
        }
        const bool keyframe = netsync_keyframe_due( b.net, tick_number );
        const bool changed = !b.net.sent || netsync_moved( b.net, b.min, b.vel );
        if ( !changed && !keyframe ) {
            continue;
        }
        NetEntityStateEntry e;
        e.id = b.net.id;
        e.kind = static_cast<uint8_t>( NetEntityKind::BODY );
        e.x = b.min.x;
        e.y = b.min.y;
        e.z = b.min.z;
        e.vx = static_cast<float>( b.vel.x );
        e.vy = static_cast<float>( b.vel.y );
        e.vz = static_cast<float>( b.vel.z );
        out.push_back( e );
        // The server now applies its boundary hysteresis to this pos.
        netsync_mark_sent( b.net, b.min, b.vel, tick_number, keyframe );
    }
}
