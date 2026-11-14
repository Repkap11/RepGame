// Rail physics for RAIL-type vehicles (the minecart sim), split out of
// vehicles.cpp: the rail helpers, the CART_* tuning constants, and the
// on-rail/derailed per-tick body. vehicle_physics.hpp is the seam the
// generic Vehicles tick dispatches through.

#include "common/vehicle_physics.hpp"

#include <algorithm>
#include <cmath>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/constants.hpp"
#include "common/rail.hpp"
#include "common/utils/collision.hpp"
#include "common/world.hpp"

// Per-tick constants at UPS_RATE (144). The design values are MC numbers
// at 20 TPS: a per-MC-tick multiplier f becomes pow(f, 20/UPS_RATE), a
// blocks/s speed divides by UPS_RATE and a blocks/s² accel by
// UPS_RATE*UPS_RATE.
static constexpr double UPS = UPS_RATE;
static constexpr double CART_MAX_SPEED = 12.0 / UPS;                          // blocks/tick
static constexpr double CART_SLOPE_ACCEL = 5.0 / ( UPS * UPS );               // downhill gravity along the path
static constexpr double CART_POWERED_BOOST = 24.0 / ( UPS * UPS );             // powered rail accel
static constexpr double CART_LAUNCH_KICK = 0.4 / UPS;                         // kick off a blocked powered rail end
static const double CART_POWERED_BRAKE = pow( 0.5, 20.0 / UPS );               // unpowered rail per-tick
static constexpr double CART_POWERED_SNAP = 0.6 / UPS;                         // brake dead-zone
static const double CART_FRICTION_RIDDEN = pow( 0.997, 20.0 / UPS );          // ridden drag
static const double CART_FRICTION_EMPTY = pow( 0.96, 20.0 / UPS );            // empty drag
static constexpr double CART_PUSH_ACCEL = 2.0 / ( UPS * UPS );                // rider push
static constexpr double CART_PUSH_MAX = 4.0 / UPS;                            // rider push caps out here
static constexpr double CART_PLAYER_PUSH = 6.0 / ( UPS * UPS );               // player shoving an empty cart on a rail
static constexpr double CART_PLAYER_PUSH_GROUND = 24.0 / ( UPS * UPS );       // same shove off-rail — ground drag is far heavier
static const double CART_AIR_DRAG = pow( 0.95, 20.0 / UPS );
static const double CART_GROUND_DRAG = pow( 0.5, 20.0 / UPS );
// Rail plate top sits 1/16 up the cell (scale 1 of 16 on the rail shape).
static constexpr double RAIL_TOP = 1.0 / 16.0;

double vehicle_wrap_180( const double d ) {
    return d - 360.0 * floor( ( d + 180.0 ) / 360.0 );
}

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

// The body's pitch lying flat against a rail: the slope's rise along the
// cart's *facing* (not its travel), so a parked or backwards-facing cart
// tilts the same way. Shared by the sim and by mirrors — pitch is not
// streamed; mirrors derive it from the rail under their position.
float cart_facing_pitch( const float yaw_deg, const glm::dvec3 &u ) {
    const double ry = glm::radians( static_cast<double>( yaw_deg ) );
    const double along = sin( ry ) * u.x - cos( ry ) * u.z;
    return static_cast<float>( glm::degrees( asin( glm::clamp( u.y * ( along >= 0 ? 1.0 : -1.0 ), -1.0, 1.0 ) ) ) );
}

} // namespace

// Mirror pitch/on_rail for a remote-owned rail vehicle: pitch is not
// streamed, so the mirror derives it from the rail under its eased
// position. Also updates v.on_rail (the mp-test dump exposes it).
float vehicle_rail_mirror_pitch( World &world, Vehicles::Vehicle &v ) {
    glm::ivec3 rcell;
    BlockState rstate;
    if ( rail_at( world, v.pos, &rcell, &rstate ) ) {
        const RailPath path = rail_path( world, rcell, rstate );
        v.on_rail = true;
        return cart_facing_pitch( v.yaw, path.u );
    }
    v.on_rail = false;
    return 0.0f;
}

bool vehicle_rail_spawn_pose( World &world, const glm::ivec3 &rail_cell, const float player_yaw, glm::dvec3 *pos, float *yaw ) {
    const BlockState s = world.get_loaded_block( rail_cell );
    if ( s.id == LAST_BLOCK_ID || !block_definition_get_definition( s.id )->is_rail ) {
        return false;
    }
    const RailPath path = rail_path( world, rail_cell, s );
    *pos = ( path.p[ 0 ] + path.p[ 1 ] ) * 0.5;
    // Face along the rail axis nearest the player's look direction.
    const glm::dvec3 d0 = path.dirs[ 0 ], d1 = path.dirs[ 1 ];
    const float yaw0 = static_cast<float>( glm::degrees( atan2( d0.x, -d0.z ) ) );
    const float yaw1 = static_cast<float>( glm::degrees( atan2( d1.x, -d1.z ) ) );
    *yaw = fabs( vehicle_wrap_180( yaw0 - player_yaw ) ) <= fabs( vehicle_wrap_180( yaw1 - player_yaw ) ) ? yaw0 : yaw1;
    return true;
}

// Advance the cart along its rail: compute the signed speed along the
// path, apply slope/gravity/powered-rail/friction/push effects, move, and
// step into the next cell across endpoints. `in` carries rider/push
// state. Off-rail the cart is ballistic with ground/air drag and box
// collision until it re-snaps onto a rail or rests.
void vehicle_tick_rail( RepGameState &state, Vehicles::Vehicle &c, const Vehicles::VehicleTickInput &in ) {
    World &world = state.world;
    const VehicleTypeDef &def = vehicle_type_def( c.type );
    glm::ivec3 cell;
    BlockState rail_state;
    if ( rail_at( world, c.pos, &cell, &rail_state ) ) {
        c.on_rail = true;
    } else {
        c.on_rail = false;
    }

    // Bodies overlapping an empty cart shove it — the local player plus
    // remote players (extra_pushers) with the same box rule; a ridden
    // cart is never pushed (local or remote rider). apply() gets the
    // normalized horizontal push direction once per overlapping body:
    // on a rail it's projected onto the path, off the rails it
    // accelerates the world-space velocity.
    const auto for_each_push = [ & ]( const auto &apply ) {
        if ( c.rider || c.rider_session != 0 ) {
            return;
        }
        const glm::dvec3 cmin = vehicle_min( def, c.pos ), cmax = vehicle_max( def, c.pos );
        const auto shove = [ & ]( const glm::dvec3 &feet ) {
            const glm::dvec3 pmin = feet - glm::dvec3( in.player_width / 2.0, 0.0, in.player_width / 2.0 );
            const glm::dvec3 pmax = feet + glm::dvec3( in.player_width / 2.0, in.player_height, in.player_width / 2.0 );
            if ( cmin.x < pmax.x && cmax.x > pmin.x && cmin.y < pmax.y && cmax.y > pmin.y && cmin.z < pmax.z && cmax.z > pmin.z ) {
                glm::dvec3 away = c.pos + glm::dvec3( 0.0, def.height * 0.5, 0.0 ) - ( feet + glm::dvec3( 0.0, in.player_height * 0.5, 0.0 ) );
                away.y = 0.0;
                if ( glm::length( away ) > 1e-6 ) {
                    apply( glm::normalize( away ) );
                }
            }
        };
        if ( !in.player_no_clip ) {
            shove( in.player_feet );
        }
        for ( const glm::dvec3 &feet : in.extra_pushers ) {
            shove( feet );
        }
    };

    if ( c.on_rail ) {
        RailPath path = rail_path( world, cell, rail_state );
        // Project onto the path: t is the distance along p0 -> p1.
        double t = glm::dot( c.pos - path.p[ 0 ], path.u );
        t = std::max( 0.0, std::min( t, path.len ) );
        double s = glm::dot( c.vel, path.u );

        // Downhill gravity: descending carts gain speed along -u.y.
        s += -path.u.y * CART_SLOPE_ACCEL;

        const bool powered_rail = rail_state.id == POWERED_RAIL;
        if ( powered_rail ) {
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
                    s += CART_POWERED_BOOST * ( s > 0 ? 1.0 : -1.0 );
                }
            } else {
                s *= CART_POWERED_BRAKE;
                if ( fabs( s ) < CART_POWERED_SNAP ) {
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
        // The shove runs along the rail — projected onto the path.
        for_each_push( [ & ]( const glm::dvec3 &dir ) {
            s += CART_PLAYER_PUSH * glm::dot( dir, path.u );
        } );

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
            c.yaw = static_cast<float>( y0 + ( fabs( vehicle_wrap_180( y1 - c.yaw ) ) < fabs( vehicle_wrap_180( y0 - c.yaw ) ) ? 180.0 : 0.0 ) );
            c.yaw = static_cast<float>( c.yaw - 360.0 * floor( ( c.yaw + 180.0 ) / 360.0 ) );
            // The body lies flat against the rail (shared with mirrors).
            c.pitch = cart_facing_pitch( c.yaw, path.u );
        }
    } else {
        // Off-rail: ballistic with ground/air drag and box collision.
        // The same overlap shove accelerates the cart in world space —
        // the player can push a grounded cart (e.g. back onto a track).
        for_each_push( [ & ]( const glm::dvec3 &dir ) {
            c.vel += dir * CART_PLAYER_PUSH_GROUND;
        } );
        c.vel.y -= GRAVITY_STRENGTH;
        if ( c.vel.y < -TERMINAL_VELOCITY ) {
            c.vel.y = -TERMINAL_VELOCITY;
        }
        glm::dvec3 min = vehicle_min( def, c.pos );
        glm::dvec3 max = vehicle_max( def, c.pos );
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
