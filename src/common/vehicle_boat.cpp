// Water physics for WATER-type vehicles (boats), beside vehicle_rail.cpp:
// buoyancy against the fluid surface, rider paddle/steer input, land
// crawl and player shoves. vehicle_physics.hpp is the seam the generic
// Vehicles tick dispatches through.

#include "common/vehicle_physics.hpp"

#include <algorithm>
#include <cmath>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/constants.hpp"
#include "common/fluid.hpp"
#include "common/utils/collision.hpp"
#include "common/world.hpp"

// Per-tick constants at UPS_RATE (144). The design values are MC numbers
// at 20 TPS converted the same way the rail TU converts its constants:
// a per-MC-tick multiplier f becomes pow(f, 20/UPS_RATE), a blocks/s
// speed divides by UPS_RATE and a blocks/s² accel by UPS_RATE*UPS_RATE.
static constexpr double UPS = UPS_RATE;
// How deep the hull sits: bottom rides this far under the surface (the
// shared constant is VEHICLE_BOAT_DRAFT in vehicle_physics.hpp). Must
// stay under the 3px floor panel thickness so the water plane renders
// under the floor, not inside the hull.
static constexpr double BOAT_DRAFT = VEHICLE_BOAT_DRAFT;
// Buoyancy: damped spring on vel.y toward bottom = surface - draft.
// Spring stiffness in blocks/s² per block of depth error; the vertical
// damping keeps a settled boat from bobbing.
static constexpr double BOAT_BUOY_ACCEL = 40.0 / ( UPS * UPS );
static const double BOAT_VERTICAL_DAMP = pow( 0.5, 20.0 / UPS );
// A fully submerged boat floats up at ~2 blocks/s.
static constexpr double BOAT_RISE_ACCEL = 30.0 / ( UPS * UPS );
static constexpr double BOAT_RISE_MAX = 2.0 / UPS;
// Rider paddle accel along the boat yaw: MC's ~0.04/tick forward, an
// eighth of it backwards.
static constexpr double BOAT_FORWARD_ACCEL = 16.0 / ( UPS * UPS );
static constexpr double BOAT_BACK_ACCEL = 2.0 / ( UPS * UPS );
// Horizontal drag: water/air MC 0.9/tick (top speed ~7.6 blocks/s);
// land 0.45/tick — the land crawl MC gives a beached boat.
static const double BOAT_FRICTION_WATER = pow( 0.9, 20.0 / UPS );
static const double BOAT_FRICTION_AIR = pow( 0.9, 20.0 / UPS );
static const double BOAT_FRICTION_LAND = pow( 0.45, 20.0 / UPS );
// Total depenetration push a hull may take per tick (movement and
// turning each): a badly embedded legacy hull escapes over a few ticks
// instead of teleporting.
static constexpr double BOAT_PUSH_CAP = 0.3;
// Rotation's depenetration budget is far smaller (~3 b/s, shared across
// the full + half-delta attempts): a pivot only needs to move a hull
// corner a hair per tick, and a refused turn must not fling the boat.
static constexpr double BOAT_TURN_PUSH_CAP = 0.02;
// Steering: turn input is an angular accel (deg/s²) against a
// 0.9-per-MC-tick angular damper -> ~180 deg/s steady rate.
static constexpr double BOAT_TURN_ACCEL = 380.0 / UPS;
static const double BOAT_TURN_DAMP = pow( 0.9, 20.0 / UPS );
// Player bumping an empty boat — same strength as the cart shove but in
// world-space XZ (no rail path projection).
static constexpr double BOAT_PLAYER_PUSH = 6.0 / ( UPS * UPS );

namespace {

enum BoatMedium { BOAT_IN_WATER, BOAT_UNDER_WATER, BOAT_ON_LAND, BOAT_IN_AIR };

// The water surface height above the boat's box bottom (world space), or
// -inf when no water is under the footprint. Samples the four rotated
// hull corners and the centre in the cells overlapping the bottom and
// the cell above; waterlogged cells count as water, lava does not
// (fluid_in_cell).
double boat_water_surface( World &world, const Vehicles::Vehicle &v, const VehicleTypeDef &def ) {
    const double ry = glm::radians( static_cast<double>( v.yaw ) );
    const glm::dvec2 fwd( sin( ry ), -cos( ry ) );
    const glm::dvec2 side( cos( ry ), sin( ry ) );
    const double hl = def.length * 0.5 - 1e-3, hw = def.width * 0.5 - 1e-3;
    const glm::dvec2 pts[ 5 ] = {
        glm::dvec2( v.pos.x, v.pos.z ) + fwd * hl + side * hw, glm::dvec2( v.pos.x, v.pos.z ) + fwd * hl - side * hw,
        glm::dvec2( v.pos.x, v.pos.z ) - fwd * hl + side * hw, glm::dvec2( v.pos.x, v.pos.z ) - fwd * hl - side * hw,
        glm::dvec2( v.pos.x, v.pos.z ),
    };
    double surface = -HUGE_VAL;
    for ( const glm::dvec2 &p : pts ) {
        for ( int dy = 0; dy <= 1; dy++ ) {
            const glm::ivec3 cell( static_cast<int>( floor( p.x ) ),
                                   static_cast<int>( floor( v.pos.y ) ) + dy,
                                   static_cast<int>( floor( p.y ) ) );
            const BlockState s = world.get_loaded_block( cell );
            if ( s.id == LAST_BLOCK_ID || fluid_in_cell( s ) != WATER ) {
                continue;
            }
            surface = std::max( surface, cell.y + static_cast<double>( fluid_surface_height( world, cell ) ) );
        }
    }
    return surface;
}

} // namespace

// The owned per-tick boat sim. States: UNDER_WATER (surface above the box
// top) rises; IN_WATER floats on a damped spring to surface - draft;
// ON_LAND crawls on voxel collision; IN_AIR falls like a derailed cart.
// Rider input: rider_forward paddles along yaw, rider_turn steers the yaw
// through a damped angular velocity. An empty boat takes player shoves in
// world-space XZ; a ridden boat never does.
void vehicle_tick_boat( RepGameState &state, Vehicles::Vehicle &c, const Vehicles::VehicleTickInput &in ) {
    World &world = state.world;
    const VehicleTypeDef &def = vehicle_type_def( c.type );
    const bool ridden = c.rider || c.rider_session != 0;

    const double surface = boat_water_surface( world, c, def );
    const bool water = surface > c.pos.y;
    const BoatMedium pre_state = !water                          ? BOAT_ON_LAND // or air — resolved by standing below
                                 : surface > c.pos.y + def.height ? BOAT_UNDER_WATER
                                                                 : BOAT_IN_WATER;

    // Vertical motion.
    if ( pre_state == BOAT_UNDER_WATER ) {
        c.vel.y = std::min( c.vel.y + BOAT_RISE_ACCEL, BOAT_RISE_MAX );
    } else if ( pre_state == BOAT_IN_WATER ) {
        // Spring+damp to the float line (no separate gravity: the spring
        // alone sets the equilibrium the settle test measures).
        c.vel.y += ( surface - BOAT_DRAFT - c.pos.y ) * BOAT_BUOY_ACCEL;
        c.vel.y *= BOAT_VERTICAL_DAMP;
    } else {
        c.vel.y -= GRAVITY_STRENGTH;
        if ( c.vel.y < -TERMINAL_VELOCITY ) {
            c.vel.y = -TERMINAL_VELOCITY;
        }
    }

    // Rider input: paddling only has traction in water or standing on
    // land (the exact hull dipped a hair below the boat).
    glm::dvec3 dip = c.pos - glm::dvec3( 0.0, 0.02, 0.0 );
    const bool on_land = vehicle_hull_penetration( world, def, dip, c.yaw ) > 0.005;
    const double fwd = ridden ? in.rider_forward : 0.0;
    const double turn = ridden ? in.rider_turn : 0.0;
    if ( water || on_land ) {
        const double accel = fwd > 0.0 ? fwd * BOAT_FORWARD_ACCEL : fwd * BOAT_BACK_ACCEL;
        const double ry = glm::radians( static_cast<double>( c.yaw ) );
        c.vel.x += sin( ry ) * accel;
        c.vel.z -= cos( ry ) * accel;
    }
    if ( turn != 0.0 || c.yaw_rate != 0.0f ) {
        c.yaw_rate += static_cast<float>( turn * BOAT_TURN_ACCEL );
        c.yaw_rate *= static_cast<float>( BOAT_TURN_DAMP );
        const double dyaw = vehicle_wrap_180( c.yaw_rate / UPS );
        if ( dyaw != 0.0 ) {
            // Rotate, then depenetrate: the hull pivots off whatever it
            // presses (positional push, no velocity change). Accept when
            // the rotated hull ends clear within the push cap; retry
            // with half the delta once; otherwise revert only the yaw
            // and kill the rate — the depenetration slide is kept since
            // it's a real escape out of a wedge (e.g. a hull rotated
            // between two posts slides out along the gap).
            const float pre_yaw = c.yaw;
            const glm::dvec3 pre_pos = c.pos;
            const double pre_pen = vehicle_hull_penetration( world, def, pre_pos, pre_yaw );
            bool accepted = false;
            double turn_budget = BOAT_TURN_PUSH_CAP;
            for ( int attempt = 0; attempt < 2 && !accepted; attempt++ ) {
                const double try_dyaw = attempt == 0 ? dyaw : dyaw * 0.5;
                c.pos = pre_pos;
                c.yaw = static_cast<float>( vehicle_wrap_180( pre_yaw + try_dyaw ) );
                glm::dvec2 turn_push;
                vehicle_hull_depenetrate( world, def, c.pos, c.yaw, turn_budget, &turn_push, nullptr );
                turn_budget -= glm::length( turn_push );
                if ( turn_budget < 0.0 ) {
                    turn_budget = 0.0;
                }
                accepted = vehicle_hull_penetration( world, def, c.pos, c.yaw ) <= 0.01;
            }
            // Revert the yaw but keep the depenetration slide only when
            // it didn't worsen the (unrotated) hull's overlap — the push
            // was computed for the rotated pose, so an unchecked keep
            // would let a held turn ratchet the hull into terrain.
            if ( !accepted ) {
                c.yaw = pre_yaw;
                c.yaw_rate = 0.0f;
                if ( vehicle_hull_penetration( world, def, c.pos, pre_yaw ) > pre_pen + 1e-3 ) {
                    c.pos = pre_pos;
                }
            }
        }
    }

    // Bodies overlapping an empty boat shove it away in world XZ — the
    // local player plus remote players (extra_pushers). A ridden boat is
    // never pushed.
    if ( !ridden ) {
        glm::dvec3 hmins[ VEHICLE_HULL_BOXES ], hmaxs[ VEHICLE_HULL_BOXES ];
        const int hn = vehicle_hull_boxes( def, c.pos, c.yaw, hmins, hmaxs );
        const auto shove = [ & ]( const glm::dvec3 &feet ) {
            const glm::dvec3 pmin = feet - glm::dvec3( in.player_width / 2.0, 0.0, in.player_width / 2.0 );
            const glm::dvec3 pmax = feet + glm::dvec3( in.player_width / 2.0, in.player_height, in.player_width / 2.0 );
            bool overlap = false;
            for ( int i = 0; i < hn && !overlap; i++ ) {
                overlap = hmins[ i ].x < pmax.x && hmaxs[ i ].x > pmin.x && hmins[ i ].y < pmax.y && hmaxs[ i ].y > pmin.y &&
                          hmins[ i ].z < pmax.z && hmaxs[ i ].z > pmin.z;
            }
            if ( overlap ) {
                glm::dvec3 away = c.pos + glm::dvec3( 0.0, def.height * 0.5, 0.0 ) - ( feet + glm::dvec3( 0.0, in.player_height * 0.5, 0.0 ) );
                away.y = 0.0;
                if ( glm::length( away ) > 1e-6 ) {
                    c.vel += glm::normalize( away ) * BOAT_PLAYER_PUSH;
                }
            }
        };
        if ( !in.player_no_clip ) {
            shove( in.player_feet );
        }
        for ( const glm::dvec3 &feet : in.extra_pushers ) {
            shove( feet );
        }
    }

    // Integrate the exact oriented hull: per substep, Y clamps against
    // only the boxes the footprint strictly overlaps, then XZ translates
    // and depenetrates — the MTV push produces wall sliding, and the
    // velocity component into each contact normal is stripped. Total
    // push per tick is capped (BOAT_PUSH_CAP) so an embedded hull can
    // never resolve to the far side of a 1-thick wall.
    bool standing = false;
    {
        const double max_delta = std::fmax( std::fabs( c.vel.x ), std::fmax( std::fabs( c.vel.y ), std::fabs( c.vel.z ) ) );
        int steps = static_cast<int>( ceil( max_delta / COLLISION_MAX_SUBSTEP ) );
        if ( steps < 1 ) {
            steps = 1;
        }
        const glm::dvec3 step = c.vel / static_cast<double>( steps );
        double push_budget = BOAT_PUSH_CAP;
        for ( int s = 0; s < steps; s++ ) {
            bool stand = false;
            const double applied_y = vehicle_hull_move_y( world, def, c.pos, c.yaw, step.y, &stand );
            standing |= stand;
            if ( fabs( applied_y - step.y ) > 1e-9 ) {
                c.vel.y = 0.0;
            }
            const glm::dvec2 pre_xz( c.pos.x, c.pos.z );
            const double pre_pen = vehicle_hull_penetration( world, def, c.pos, c.yaw );
            c.pos.x += step.x;
            c.pos.z += step.z;
            glm::dvec2 applied( 0.0 ), normals[ 4 ];
            int nn = vehicle_hull_depenetrate( world, def, c.pos, c.yaw, push_budget, &applied, normals );
            // Reject a substep that leaves the hull deeper than it
            // started: opposing-wall MTVs could otherwise ratchet it
            // further into terrain.
            if ( vehicle_hull_penetration( world, def, c.pos, c.yaw ) > pre_pen + 1e-3 ) {
                c.pos.x = pre_xz.x;
                c.pos.z = pre_xz.y;
                applied = glm::dvec2( 0.0 );
                nn = 0;
            }
            push_budget -= glm::length( applied );
            if ( push_budget < 0.0 ) {
                push_budget = 0.0;
            }
            for ( int k = 0; k < nn; k++ ) {
                const glm::dvec2 vn( c.vel.x, c.vel.z );
                const double into = glm::dot( vn, normals[ k ] );
                if ( into < 0.0 ) {
                    c.vel.x -= normals[ k ].x * into;
                    c.vel.z -= normals[ k ].y * into;
                }
            }
        }
    }

    // Horizontal drag by medium (applied post-move so the move saw the
    // un-damped step).
    const double drag = water ? BOAT_FRICTION_WATER
                      : standing ? BOAT_FRICTION_LAND
                                 : BOAT_FRICTION_AIR;
    c.vel.x *= drag;
    c.vel.z *= drag;
    c.pitch = 0.0f;
}
