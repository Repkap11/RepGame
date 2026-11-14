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
// -inf when no water is under the footprint. Samples the four box corners
// and the centre in the cells overlapping the bottom and the cell above;
// waterlogged cells count as water, lava does not (fluid_in_cell).
double boat_water_surface( World &world, const Vehicles::Vehicle &v, const VehicleTypeDef &def ) {
    const double hw = def.width * 0.5 - 1e-3;
    const glm::dvec2 pts[ 5 ] = {
        glm::dvec2( v.pos.x - hw, v.pos.z - hw ), glm::dvec2( v.pos.x + hw, v.pos.z - hw ),
        glm::dvec2( v.pos.x - hw, v.pos.z + hw ), glm::dvec2( v.pos.x + hw, v.pos.z + hw ),
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
    // land (tested by the box a hair below the hull).
    glm::dvec3 smin = vehicle_min( def, c.pos );
    glm::dvec3 smax = vehicle_max( def, c.pos );
    smin.y -= 0.02;
    smax.y -= 0.02;
    const bool on_land = Collision::box_collides( world, smin, smax );
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
        c.yaw += static_cast<float>( c.yaw_rate / UPS );
        c.yaw = static_cast<float>( vehicle_wrap_180( c.yaw ) );
    }

    // Bodies overlapping an empty boat shove it away in world XZ — the
    // local player plus remote players (extra_pushers). A ridden boat is
    // never pushed.
    if ( !ridden ) {
        const glm::dvec3 cmin = vehicle_min( def, c.pos ), cmax = vehicle_max( def, c.pos );
        const auto shove = [ & ]( const glm::dvec3 &feet ) {
            const glm::dvec3 pmin = feet - glm::dvec3( in.player_width / 2.0, 0.0, in.player_width / 2.0 );
            const glm::dvec3 pmax = feet + glm::dvec3( in.player_width / 2.0, in.player_height, in.player_width / 2.0 );
            if ( cmin.x < pmax.x && cmax.x > pmin.x && cmin.y < pmax.y && cmax.y > pmin.y && cmin.z < pmax.z && cmax.z > pmin.z ) {
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

    // Integrate through voxel collision; a blocked axis' velocity dies.
    glm::dvec3 min = vehicle_min( def, c.pos );
    glm::dvec3 max = vehicle_max( def, c.pos );
    bool hit_axis[ 3 ] = { false, false, false };
    int hit_extra[ 3 ] = { -1, -1, -1 };
    bool standing = false;
    const std::vector<BodyBox> no_extra;
    Collision::move_box( world, min, max, c.vel, no_extra, hit_axis, hit_extra, &standing );
    c.pos = glm::dvec3( ( min.x + max.x ) * 0.5, min.y, ( min.z + max.z ) * 0.5 );
    for ( int a = 0; a < 3; a++ ) {
        if ( hit_axis[ a ] ) {
            c.vel[ a ] = 0.0;
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
