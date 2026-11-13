#include "common/minecarts.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "common/RepGame.hpp"
#include "common/block.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"
#include "common/chunk_loader.hpp"
#include "common/mobs.hpp" // mob_bone_worlds / mob_cube_matrix + mob_models
#include "common/rail.hpp"
#include "common/utils/collision.hpp"
#include "common/utils/env_config.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/world.hpp"

// Per-tick constants at UPS_RATE (144). The design values are MC numbers
// at 20 TPS: a per-MC-tick multiplier f becomes pow(f, 20/UPS_RATE), a
// blocks/s speed divides by UPS_RATE and a blocks/s² accel by
// UPS_RATE*UPS_RATE.
static constexpr double UPS = UPS_RATE;
static constexpr double CART_MAX_SPEED = 8.0 / UPS;                           // blocks/tick
static constexpr double CART_SLOPE_ACCEL = 3.125 / ( UPS * UPS );             // downhill gravity along the path
static constexpr double CART_GOLDEN_BOOST = 24.0 / ( UPS * UPS );             // powered golden rail accel
static constexpr double CART_LAUNCH_KICK = 0.4 / UPS;                         // kick off a blocked powered rail end
static const double CART_GOLDEN_BRAKE = pow( 0.5, 20.0 / UPS );               // unpowered golden per-tick
static constexpr double CART_GOLDEN_SNAP = 0.6 / UPS;                         // brake dead-zone
static const double CART_FRICTION_RIDDEN = pow( 0.997, 20.0 / UPS );          // ridden drag
static const double CART_FRICTION_EMPTY = pow( 0.96, 20.0 / UPS );            // empty drag
static constexpr double CART_PUSH_ACCEL = 2.0 / ( UPS * UPS );                // rider push
static constexpr double CART_PUSH_MAX = 4.0 / UPS;                            // rider push caps out here
static constexpr double CART_PLAYER_PUSH = 6.0 / ( UPS * UPS );               // player shoving an empty cart
static const double CART_AIR_DRAG = pow( 0.95, 20.0 / UPS );
static const double CART_GROUND_DRAG = pow( 0.5, 20.0 / UPS );
// Rail plate top sits 1/16 up the cell (scale 1 of 16 on the rail shape).
static constexpr double RAIL_TOP = 1.0 / 16.0;
// Survival hits accumulate 1 damage each and decay at this rate per tick —
// three hits inside ~2 ticks of each other break (decay 1 per ~0.7 s).
static constexpr float CART_DAMAGE_PER_HIT = 1.0f;
static constexpr float CART_DAMAGE_DECAY = 2.0f / static_cast<float>( UPS_RATE );
static constexpr int CART_HURT_WOBBLE_TICKS = UPS_RATE / 2;

namespace {

// The rail cell holding a cart: the cell at floor(pos) or the one below
// (a cart just past a slope's top sits over the lower rail). Returns
// whether `*state` is a rail and fills it + the cell position.
bool rail_at( World &world, const glm::dvec3 &pos, glm::ivec3 *cell, BlockState *state ) {
    const glm::ivec3 base( glm::floor( pos ) );
    for ( int dy = 0; dy >= -1; dy-- ) {
        const glm::ivec3 c = base + glm::ivec3( 0, dy, 0 );
        const BlockState s = world.get_loaded_block( c );
        if ( s.id != LAST_BLOCK_ID && block_definition_get_definition( s.id )->is_rail ) {
            // The lower candidate only counts when the cart's bottom is at
            // or below that rail's surface — otherwise it's a rail in the
            // floor of the cell above the cart, not under it.
            if ( dy == -1 && pos.y > c.y + 1.0 + RAIL_TOP + 0.01 ) {
                continue;
            }
            if ( cell != nullptr ) {
                *cell = c;
            }
            if ( state != nullptr ) {
                *state = s;
            }
            return true;
        }
    }
    return false;
}

// A rail track segment through one cell: a straight line between the two
// endpoint edge-midpoints (a curve's "path" is the diagonal between its
// two edge mids, like MC). p0/p1 are world positions of the cart's
// bottom-centre at each end (plate-top height included); u is the unit
// direction p0 -> p1 and len the segment length.
struct RailPath {
    glm::ivec3 cell;
    RailShape shape;
    BlockState state;
    glm::ivec3 dirs[ 2 ];
    int dy[ 2 ];
    glm::dvec3 p[ 2 ];
    glm::dvec3 u;
    double len;
};

RailPath rail_path( World &world, const glm::ivec3 &cell, const BlockState &state ) {
    RailPath path;
    path.cell = cell;
    path.shape = rail_shape_of( state );
    path.state = state;
    rail_endpoints( path.shape, path.dirs, path.dy );
    for ( int i = 0; i < 2; i++ ) {
        path.p[ i ] = glm::dvec3( cell ) + glm::dvec3( 0.5, 0.0, 0.5 ) + glm::dvec3( path.dirs[ i ] ) * 0.5 +
                      glm::dvec3( 0.0, static_cast<double>( path.dy[ i ] ) + RAIL_TOP, 0.0 );
    }
    const glm::dvec3 d = path.p[ 1 ] - path.p[ 0 ];
    path.len = glm::length( d );
    path.u = path.len > 1e-9 ? d / path.len : glm::dvec3( 1.0, 0.0, 0.0 );
    return path;
}

// Which of `path`'s endpoints exits toward `dir` (a horizontal unit
// vector), or -1. An ascending end's dy lives in the path, so only the
// horizontal direction is compared.
int path_end_facing( const RailPath &path, const glm::ivec3 &dir ) {
    for ( int i = 0; i < 2; i++ ) {
        if ( path.dirs[ i ] == dir ) {
            return i;
        }
    }
    return -1;
}

bool cell_solid( World &world, const glm::ivec3 &cell ) {
    const BlockState s = world.get_loaded_block( cell );
    return s.id == LAST_BLOCK_ID || block_definition_get_definition( s.id )->collides_with_player;
}

// Step off `path` through endpoint `end` into the next rail cell.
// Candidates: the cell the endpoint exits into at its own height (dirs[e]
// + dy[e] up), then — for flat exits — one down where a rail ascending
// toward us picks up the drop. Returns true and fills `next` when a rail
// is there (entry_end is that rail's endpoint facing back at us), false
// on an open track end (derail) or a blocked cell (blocked set).
bool next_rail_cell( World &world, const RailPath &path, int end, RailPath *next, int *entry_end, bool *blocked ) {
    *blocked = false;
    const glm::ivec3 dir = path.dirs[ end ];
    const glm::ivec3 candidates[ 2 ] = {
        path.cell + dir + glm::ivec3( 0, path.dy[ end ], 0 ),
        path.cell + dir + glm::ivec3( 0, -1, 0 ),
    };
    for ( int ci = 0; ci < 2; ci++ ) {
        const glm::ivec3 cell = candidates[ ci ];
        if ( ci == 1 && path.dy[ end ] != 0 ) {
            continue; // already-climbing exits only continue at their level
        }
        const BlockState s = world.get_loaded_block( cell );
        if ( s.id == LAST_BLOCK_ID ) {
            *blocked = true;
            return false; // unloaded counts as solid
        }
        const Block *def = block_definition_get_definition( s.id );
        if ( def->is_rail ) {
            RailPath np = rail_path( world, cell, s );
            const int j = path_end_facing( np, -dir );
            if ( j < 0 ) {
                continue; // rail there, but not connected to our exit
            }
            if ( ci == 1 ) {
                // A lower rail only connects upward when its end facing us
                // is the climbing one — else our edge would hover a block
                // above the lower plate.
                if ( np.dy[ j ] == 0 ) {
                    continue;
                }
            }
            *next = np;
            *entry_end = j;
            return true;
        }
        if ( def->collides_with_player && ci == 0 ) {
            // The cell the cart's body would enter is solid — stop at the
            // endpoint. A solid *lower* cell is just the landing floor.
            *blocked = true;
            return false;
        }
        // Air or non-colliding — try the lower candidate.
    }
    return false;
}

double wrap_180( double d ) {
    return d - 360.0 * floor( ( d + 180.0 ) / 360.0 );
}

} // namespace

void Minecarts::init( const VertexBufferLayout &vbl_object_vertex, const VertexBufferLayout &vbl_mob_instance ) {
    this->render_chain.init( vbl_object_vertex, vbl_mob_instance, vd_data_player_object, VB_DATA_SIZE_PARTICLE, ib_data_solid, IB_SOLID_SIZE );
    this->render_inited = true;
}

void Minecarts::init_storage( MapStorage *s ) {
    this->storage = s;
}

uint32_t Minecarts::spawn( const glm::dvec3 &pos, float yaw, int persistent ) {
    if ( this->mp_mode || this->carts.size( ) >= MAX_CARTS ) {
        return 0;
    }
    Cart c;
    c.id = this->next_id++;
    c.pos = c.prev_pos = pos;
    c.yaw = c.prev_yaw = yaw;
    c.persistent = persistent;
    if ( this->render_inited ) {
        for ( int i = 0; i < mob_models[ MOB_MODEL_MINECART ].cube_count; i++ ) {
            const std::pair<entt::entity, MobPartInstance &> data = this->render_chain.create_instance( );
            c.parts.push_back( data.first );
        }
    }
    this->carts.push_back( c );
    return c.id;
}

uint32_t Minecarts::spawn_on_rail( World &world, const glm::ivec3 &rail_cell, float player_yaw ) {
    const BlockState s = world.get_loaded_block( rail_cell );
    if ( s.id == LAST_BLOCK_ID || !block_definition_get_definition( s.id )->is_rail ) {
        return 0;
    }
    const RailPath path = rail_path( world, rail_cell, s );
    const glm::dvec3 pos = ( path.p[ 0 ] + path.p[ 1 ] ) * 0.5;
    // Face along the rail axis nearest the player's look direction.
    const glm::dvec3 d0 = path.dirs[ 0 ], d1 = path.dirs[ 1 ];
    const float yaw0 = static_cast<float>( glm::degrees( atan2( d0.x, -d0.z ) ) );
    const float yaw1 = static_cast<float>( glm::degrees( atan2( d1.x, -d1.z ) ) );
    const float yaw = fabs( wrap_180( yaw0 - player_yaw ) ) <= fabs( wrap_180( yaw1 - player_yaw ) ) ? yaw0 : yaw1;
    const uint32_t id = this->spawn( pos, yaw );
    if ( id != 0 ) {
        this->carts.back( ).on_rail = true;
    }
    return id;
}

bool Minecarts::cart_chunk_live( World &world, const Cart &c ) const {
    const Chunk *chunk = world.get_loaded_chunk( glm::ivec3( glm::floor( c.pos ) ) );
    return chunk != nullptr && chunk->load_state == CHUNK_LIVE;
}

static glm::dvec3 cart_min( const glm::dvec3 &pos ) {
    return pos - glm::dvec3( Minecarts::CART_WIDTH / 2.0, 0.0, Minecarts::CART_WIDTH / 2.0 );
}
static glm::dvec3 cart_max( const glm::dvec3 &pos ) {
    return pos + glm::dvec3( Minecarts::CART_WIDTH / 2.0, Minecarts::CART_HEIGHT, Minecarts::CART_WIDTH / 2.0 );
}

// Advance the cart along its rail: compute the signed speed along the
// path, apply slope/gravity/golden/friction/push effects, move, and step
// into the next cell across endpoints. `in` carries rider/push state.
void Minecarts::tick( RepGameState &state, const CartTickInput &in ) {
    World &world = state.world;
    const bool freeze = RepGame::capture_mode( );
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );

    for ( Cart &c : this->carts ) {
        c.prev_pos = c.pos;
        c.prev_yaw = c.yaw;
        c.prev_pitch = c.pitch;
        if ( freeze || !cart_chunk_live( world, c ) ) {
            continue;
        }
        c.rider = c.id != 0 && c.id == in.rider_cart;
        if ( c.hurt_ticks > 0 ) {
            c.hurt_ticks--;
        }
        if ( c.damage > 0.0f ) {
            c.damage = std::max( 0.0f, c.damage - CART_DAMAGE_DECAY );
        }

        glm::ivec3 cell;
        BlockState rail_state;
        if ( rail_at( world, c.pos, &cell, &rail_state ) ) {
            c.on_rail = true;
        } else {
            c.on_rail = false;
        }

        if ( c.on_rail ) {
            RailPath path = rail_path( world, cell, rail_state );
            // Project onto the path: t is the distance along p0 -> p1.
            double t = glm::dot( c.pos - path.p[ 0 ], path.u );
            t = std::max( 0.0, std::min( t, path.len ) );
            double s = glm::dot( c.vel, path.u );

            // Downhill gravity: descending carts gain speed along -u.y.
            s += -path.u.y * CART_SLOPE_ACCEL;

            const bool golden = rail_state.id == GOLDEN_RAIL;
            if ( golden ) {
                const bool powered = rail_state.current_redstone_power > 0;
                if ( powered ) {
                    // Launch kick: a rest cart on a powered rail with a
                    // blocked end is shoved away from it (MC behaviour —
                    // lets a powered rail start a parked cart).
                    if ( fabs( s ) < 1e-6 ) {
                        for ( int e = 0; e < 2; e++ ) {
                            const glm::ivec3 blocked_cell = path.cell + path.dirs[ e ] + glm::ivec3( 0, path.dy[ e ], 0 );
                            if ( cell_solid( world, blocked_cell ) ) {
                                const double kick_dir = e == 0 ? 1.0 : -1.0; // away from the blocked end
                                s += kick_dir * CART_LAUNCH_KICK;
                            }
                        }
                    }
                    if ( fabs( s ) >= 1e-6 ) {
                        s += CART_GOLDEN_BOOST * ( s > 0 ? 1.0 : -1.0 );
                    }
                } else {
                    s *= CART_GOLDEN_BRAKE;
                    if ( fabs( s ) < CART_GOLDEN_SNAP ) {
                        s = 0.0;
                    }
                }
            }

            // Friction: empty carts drag harder than ridden ones.
            s *= c.rider ? CART_FRICTION_RIDDEN : CART_FRICTION_EMPTY;

            // Rider push: accelerates along the rail's projection of the
            // input direction while under the push cap.
            if ( c.rider && ( in.rider_push.x != 0.0 || in.rider_push.y != 0.0 ) && fabs( s ) < CART_PUSH_MAX ) {
                const glm::dvec3 push_dir = glm::normalize( glm::dvec3( in.rider_push.x, 0.0, in.rider_push.y ) );
                s += CART_PUSH_ACCEL * glm::dot( push_dir, path.u );
            }
            // The player's body overlapping an empty cart shoves it along
            // the rail away from the player.
            if ( !c.rider && !in.player_no_clip ) {
                const glm::dvec3 pmin = in.player_feet - glm::dvec3( in.player_width / 2.0, 0.0, in.player_width / 2.0 );
                const glm::dvec3 pmax = in.player_feet + glm::dvec3( in.player_width / 2.0, in.player_height, in.player_width / 2.0 );
                const glm::dvec3 cmin = cart_min( c.pos ), cmax = cart_max( c.pos );
                if ( cmin.x < pmax.x && cmax.x > pmin.x && cmin.y < pmax.y && cmax.y > pmin.y && cmin.z < pmax.z && cmax.z > pmin.z ) {
                    glm::dvec3 away = c.pos + glm::dvec3( 0.0, CART_HEIGHT * 0.5, 0.0 ) - ( in.player_feet + glm::dvec3( 0.0, in.player_height * 0.5, 0.0 ) );
                    away.y = 0.0;
                    if ( glm::length( away ) > 1e-6 ) {
                        s += CART_PLAYER_PUSH * glm::dot( glm::normalize( away ), path.u );
                    }
                }
            }

            s = std::max( -CART_MAX_SPEED, std::min( s, CART_MAX_SPEED ) );

            // Advance along the path, stepping into neighbouring rail
            // cells across endpoints with the leftover distance.
            t += s;
            int guard = 0;
            bool derailed = false;
            while ( ( t < 0.0 || t > path.len ) && guard++ < 8 ) {
                const int end = t > path.len ? 1 : 0;
                const double leftover = end == 1 ? t - path.len : -t;
                RailPath next;
                int entry_end = -1;
                bool blocked = false;
                if ( !next_rail_cell( world, path, end, &next, &entry_end, &blocked ) ) {
                    if ( blocked ) {
                        // Stopped at the endpoint — clamp just inside it so
                        // the cart still counts as on this rail cell (floor
                        // of an exact-boundary position reads the next cell).
                        t = end == 1 ? path.len - 1e-6 : 1e-6;
                        s = 0.0;
                    } else {
                        // End of track: derail with world-space velocity.
                        derailed = true;
                        c.on_rail = false;
                        c.vel = path.u * s;
                        c.pos = end == 1 ? path.p[ 1 ] : path.p[ 0 ];
                    }
                    break;
                }
                // Enter through entry_end moving away from it.
                const double mag = fabs( s );
                s = entry_end == 0 ? mag : -mag;
                t = entry_end == 0 ? leftover : next.len - leftover;
                path = next;
            }
            if ( !derailed ) {
                c.pos = path.p[ 0 ] + path.u * t;
                c.vel = path.u * s;
            }

            // Face along travel; keep yaw continuity (a cart can run a
            // track backwards — pick the nearer of dir / dir+180).
            const glm::dvec3 travel = path.u * ( s >= 0 ? 1.0 : -1.0 );
            if ( !derailed && ( fabs( travel.x ) > 1e-6 || fabs( travel.z ) > 1e-6 ) ) {
                const double y0 = glm::degrees( atan2( travel.x, -travel.z ) );
                const double y1 = y0 + 180.0;
                c.yaw = static_cast<float>( y0 + ( fabs( wrap_180( y1 - c.yaw ) ) < fabs( wrap_180( y0 - c.yaw ) ) ? 180.0 : 0.0 ) );
                c.yaw = static_cast<float>( c.yaw - 360.0 * floor( ( c.yaw + 180.0 ) / 360.0 ) );
                c.pitch = static_cast<float>( glm::degrees( asin( glm::clamp( path.u.y * ( s >= 0 ? 1.0 : -1.0 ), -1.0, 1.0 ) ) ) );
            }
        } else {
            // Off-rail: ballistic with ground/air drag and box collision.
            c.vel.y -= GRAVITY_STRENGTH;
            if ( c.vel.y < -TERMINAL_VELOCITY ) {
                c.vel.y = -TERMINAL_VELOCITY;
            }
            glm::dvec3 min = cart_min( c.pos );
            glm::dvec3 max = cart_max( c.pos );
            bool hit_axis[ 3 ] = { false, false, false };
            int hit_extra[ 3 ] = { -1, -1, -1 };
            bool standing = false;
            const std::vector<BodyBox> no_extra;
            Collision::move_box( world, min, max, c.vel, no_extra, hit_axis, hit_extra, &standing );
            c.pos = glm::dvec3( ( min.x + max.x ) * 0.5, min.y, ( min.z + max.z ) * 0.5 );
            const double drag = standing ? CART_GROUND_DRAG : CART_AIR_DRAG;
            for ( int a = 0; a < 3; a++ ) {
                if ( hit_axis[ a ] ) {
                    c.vel[ a ] = 0.0;
                } else if ( a != 1 ) {
                    c.vel[ a ] *= drag;
                }
            }
            // Re-snap: entering a rail cell (or the cell above a rail)
            // while falling/moving puts the cart back on track.
            glm::ivec3 rcell;
            BlockState rstate;
            if ( rail_at( world, c.pos, &rcell, &rstate ) ) {
                RailPath path = rail_path( world, rcell, rstate );
                double t = glm::dot( c.pos - path.p[ 0 ], path.u );
                t = std::max( 0.0, std::min( t, path.len ) );
                c.pos = path.p[ 0 ] + path.u * t;
                c.on_rail = true;
            }
        }
    }

    // Cart-cart collisions: overlapping boxes separate along the line
    // between them and exchange along-rail speeds (equal mass).
    for ( size_t i = 0; !freeze && i < this->carts.size( ); i++ ) {
        for ( size_t j = i + 1; j < this->carts.size( ); j++ ) {
            Cart &a = this->carts[ i ];
            Cart &b = this->carts[ j ];
            if ( !cart_chunk_live( world, a ) || !cart_chunk_live( world, b ) ) {
                continue;
            }
            const glm::dvec3 amin = cart_min( a.pos ), amax = cart_max( a.pos );
            const glm::dvec3 bmin = cart_min( b.pos ), bmax = cart_max( b.pos );
            if ( !( amin.x < bmax.x && amax.x > bmin.x && amin.y < bmax.y && amax.y > bmin.y && amin.z < bmax.z && amax.z > bmin.z ) ) {
                continue;
            }
            glm::dvec3 sep = ( a.pos - b.pos );
            sep.y = 0.0;
            double dist = glm::length( sep );
            glm::dvec3 n = dist > 1e-6 ? sep / dist : glm::dvec3( 1.0, 0.0, 0.0 );
            const double overlap = CART_WIDTH - dist;
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
    (void)csize;
}

int Minecarts::raycast( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist, double *out_dist ) const {
    int best = -1;
    double best_dist = max_dist;
    for ( size_t i = 0; i < this->carts.size( ); i++ ) {
        const Cart &c = this->carts[ i ];
        double d = 0.0;
        if ( RayTraversal::ray_aabb_entry( origin, dir, cart_min( c.pos ), cart_max( c.pos ), best_dist, &d ) && d < best_dist ) {
            best_dist = d;
            best = static_cast<int>( i );
        }
    }
    if ( best >= 0 && out_dist != nullptr ) {
        *out_dist = best_dist;
    }
    return best;
}

int Minecarts::resolve_target( const glm::dvec3 &origin, const glm::dvec3 &dir, double max_dist,
                               int selectionInBounds, const glm::ivec3 &pos_destroy, double *out_dist ) const {
    double cart_dist = 0.0;
    const int cart = this->raycast( origin, dir, max_dist, &cart_dist );
    if ( cart < 0 ) {
        return -1;
    }
    double block_dist = 0.0;
    const bool block_hit = selectionInBounds && RayTraversal::ray_aabb_entry( origin, dir, glm::dvec3( pos_destroy ), glm::dvec3( pos_destroy ) + glm::dvec3( 1.0 ), max_dist, &block_dist );
    if ( block_hit && block_dist <= cart_dist ) {
        return -1;
    }
    if ( out_dist != nullptr ) {
        *out_dist = cart_dist;
    }
    return cart;
}

void Minecarts::remove_cart( size_t i ) {
    this->drop_render_parts( this->carts[ i ] );
    this->carts.erase( this->carts.begin( ) + static_cast<long>( i ) );
}

bool Minecarts::hurt( int index, bool survival, bool *drop ) {
    if ( index < 0 || index >= static_cast<int>( this->carts.size( ) ) ) {
        return false;
    }
    Cart &c = this->carts[ index ];
    if ( !survival ) {
        if ( drop != nullptr ) {
            *drop = false;
        }
        remove_cart( index );
        return true;
    }
    c.damage += CART_DAMAGE_PER_HIT;
    c.hurt_ticks = CART_HURT_WOBBLE_TICKS;
    if ( c.damage >= 3.0f ) {
        if ( drop != nullptr ) {
            *drop = true;
        }
        remove_cart( index );
        return true;
    }
    return false;
}

int Minecarts::index_of( uint32_t id ) const {
    for ( size_t i = 0; i < this->carts.size( ); i++ ) {
        if ( this->carts[ i ].id == id ) {
            return static_cast<int>( i );
        }
    }
    return -1;
}

Minecarts::Cart *Minecarts::find( uint32_t id ) {
    const int i = this->index_of( id );
    return i < 0 ? nullptr : &this->carts[ i ];
}

// Same test-mode gate as the chunk's mobs/items loads: no cart file reads
// or writes under REPGAME_MOBS=0 / REPGAME_MOB_TEST.
static bool cart_persistence_enabled( ) {
    static const bool enabled = env_bool( "REPGAME_MOBS", true ) && !env_is_set( "REPGAME_MOB_TEST" );
    return enabled;
}

void Minecarts::adopt_chunk( Chunk &chunk ) {
    // Multiplayer mode: entity files live server-side; nothing local to load.
    if ( this->storage == nullptr || !cart_persistence_enabled( ) || this->mp_mode ) {
        chunk.sidecars.carts.clear( );
        return;
    }
    for ( const OwnedCartChunk &owned : this->owned_chunks ) {
        if ( owned.pos == chunk.chunk_pos ) {
            chunk.sidecars.carts.clear( );
            return;
        }
    }
    for ( const CartFileRecord &record : chunk.sidecars.carts ) {
        const uint32_t id = this->spawn( glm::dvec3( record.x, record.y, record.z ), record.yaw );
        if ( id != 0 ) {
            this->carts.back( ).vel = glm::dvec3( record.vx, record.vy, record.vz );
        }
    }
    this->owned_chunks.push_back( { chunk.chunk_pos, chunk.sidecars.carts_file_present } );
    chunk.sidecars.carts.clear( );
}

void Minecarts::poll_owned( World &world ) {
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( size_t i = 0; i < this->owned_chunks.size( ); ) {
        const OwnedCartChunk &owned = this->owned_chunks[ i ];
        const Chunk *chunk = world.get_loaded_chunk( owned.pos * glm::ivec3( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z ) );
        const bool left = chunk == nullptr || chunk->chunk_pos != owned.pos || chunk->load_state == CHUNK_PARKED;
        if ( !left ) {
            i++;
            continue;
        }
        std::vector<CartFileRecord> records;
        for ( size_t j = 0; j < this->carts.size( ); ) {
            Cart &c = this->carts[ j ];
            const glm::ivec3 owner = glm::ivec3( glm::floor( c.pos / csize ) );
            if ( c.persistent && owner == owned.pos ) {
                CartFileRecord record = { c.pos.x, c.pos.y, c.pos.z,
                                          static_cast<float>( c.vel.x ), static_cast<float>( c.vel.y ), static_cast<float>( c.vel.z ),
                                          c.yaw };
                records.push_back( record );
                remove_cart( j );
            } else {
                j++;
            }
        }
        // Rewrite (even empty) when there was a file so stale records don't
        // resurrect, or when there is something to record.
        if ( this->storage != nullptr && cart_persistence_enabled( ) && !RepGame::capture_mode( ) && !RepGame::fly_mode( ) && ( !records.empty( ) || owned.file_present ) ) {
            this->storage->persist_carts( owned.pos, records );
        }
        this->owned_chunks.erase( this->owned_chunks.begin( ) + static_cast<long>( i ) );
    }
}

void Minecarts::persist_all( ) {
    if ( this->storage == nullptr || !cart_persistence_enabled( ) || RepGame::capture_mode( ) || RepGame::fly_mode( ) || this->mp_mode ) {
        return;
    }
    const glm::dvec3 csize( CHUNK_SIZE_X, CHUNK_SIZE_Y, CHUNK_SIZE_Z );
    for ( const OwnedCartChunk &owned : this->owned_chunks ) {
        std::vector<CartFileRecord> records;
        for ( const Cart &c : this->carts ) {
            if ( c.persistent && glm::ivec3( glm::floor( c.pos / csize ) ) == owned.pos ) {
                CartFileRecord record = { c.pos.x, c.pos.y, c.pos.z,
                                          static_cast<float>( c.vel.x ), static_cast<float>( c.vel.y ), static_cast<float>( c.vel.z ),
                                          c.yaw };
                records.push_back( record );
            }
        }
        if ( !records.empty( ) || owned.file_present ) {
            this->storage->persist_carts( owned.pos, records );
        }
    }
}

void Minecarts::drop_render_parts( Cart &c ) {
    if ( !this->render_inited ) {
        return;
    }
    for ( const entt::entity part : c.parts ) {
        this->render_chain.remove( part );
    }
    c.parts.clear( );
}

void Minecarts::update_render( float alpha, const ChunkLoader &loader ) {
    if ( !this->render_inited ) {
        return;
    }
    const MobModelDef &model = mob_models[ MOB_MODEL_MINECART ];
    glm::mat4 bone_world[ 32 ];
    mob_bone_worlds( model, nullptr, nullptr, bone_world );

    for ( Cart &c : this->carts ) {
        const glm::dvec3 rpos = glm::mix( c.prev_pos, c.pos, static_cast<double>( alpha ) );
        const float ryaw = c.prev_yaw + static_cast<float>( wrap_180( c.yaw - c.prev_yaw ) ) * alpha;
        const float rpitch = c.prev_pitch + ( c.pitch - c.prev_pitch ) * alpha;

        // translate(lerp pos) · rotY(yaw) · rotX(pitch) · wobble roll ·
        // scale(1/16) · bone · cube — same base convention as mobs (model
        // -Z front lands on forward = (sin yaw, 0, -cos yaw)). The minecart
        // model's long axis is +x, so a -90° yaw pre-rot turns the cart
        // nose onto -Z.
        glm::mat4 base = glm::translate( glm::mat4( 1.0f ), glm::vec3( rpos ) );
        base = glm::rotate( base, glm::radians( -ryaw ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
        base = glm::rotate( base, glm::radians( -rpitch ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
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
        base = glm::scale( base, glm::vec3( 1.0f / 16.0f ) );

        const glm::vec2 light = loader.light_sample( glm::vec3( rpos ) + glm::vec3( 0.0f, CART_HEIGHT * 0.5f, 0.0f ) );

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
                inst.uv_b = glm::vec4( cube.size[ 2 ], static_cast<float>( MOB_SKIN_MINECART ),
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

void Minecarts::draw( const Renderer &renderer, const Shader &shader ) {
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

void Minecarts::cleanup( ) {
    for ( Cart &c : this->carts ) {
        this->drop_render_parts( c );
    }
    this->carts.clear( );
    this->owned_chunks.clear( );
}
