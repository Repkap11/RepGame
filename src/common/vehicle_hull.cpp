// Exact oriented-hull collision for WATER vehicles. The hull's
// footprint — a union of concentric oriented rectangles at yaw (the
// compound def.hull_rects, or the single def.length x def.width rect
// when a type has none), extruded def.height — is SAT-tested in 2D
// against the terrain collision boxes under it (axes: world x, world z,
// hull forward, hull side), so movement/rotation can
// move-then-depenetrate and slide along walls instead of the old
// overcovered sub-box grid. vehicle_boat.cpp drives these helpers;
// vehicle_hull_collides is the shared fit test.

#include "common/vehicle_physics.hpp"

#include <algorithm>
#include <cmath>

#include "common/utils/collision.hpp"
#include "common/world.hpp"

namespace {

// Contact skin: overlaps at or below this are "touching", not
// penetration — a hull resting a hair off a wall still pivots.
constexpr double HULL_SKIN = 1e-3;
// Terrain boxes gathered under a hull (footprint is at most ~2x3 cells;
// 8x3x4 cells x FENCE_MAX_BOXES is ample).
constexpr int TERRAIN_MAX = 512;

struct HullObb {
    glm::dvec2 c, fwd, side; // xz centre + unit axes
    double hl, hw;           // half length / half width
};

constexpr int HULL_MAX_RECTS = 4;

// Builds the hull's oriented rect(s): the def's compound hull when it
// has one, else the single def.length x def.width rect. All share the
// centre and yaw axes.
int hull_obbs( const VehicleTypeDef &def, const glm::dvec3 &pos, const float yaw, HullObb *out ) {
    const double ry = glm::radians( static_cast<double>( yaw ) );
    const glm::dvec2 fwd( sin( ry ), -cos( ry ) );
    const glm::dvec2 side( cos( ry ), sin( ry ) );
    const glm::dvec2 c( pos.x, pos.z );
    if ( def.hull_rect_count <= 0 ) {
        out[ 0 ] = HullObb{ c, fwd, side, def.length * 0.5, def.width * 0.5 };
        return 1;
    }
    const int n = std::min( def.hull_rect_count, HULL_MAX_RECTS );
    for ( int i = 0; i < n; i++ ) {
        out[ i ] = HullObb{ c, fwd, side, def.hull_rects[ i ].half_length, def.hull_rects[ i ].half_width };
    }
    return n;
}

// XZ bounding extents of the union (shared axes -> the largest half
// extent on each axis).
void hull_bounds( const HullObb *obbs, const int n, double *ex, double *ez ) {
    double hl = 0.0, hw = 0.0;
    for ( int i = 0; i < n; i++ ) {
        hl = std::max( hl, obbs[ i ].hl );
        hw = std::max( hw, obbs[ i ].hw );
    }
    *ex = hl * fabs( obbs[ 0 ].fwd.x ) + hw * fabs( obbs[ 0 ].side.x );
    *ez = hl * fabs( obbs[ 0 ].fwd.y ) + hw * fabs( obbs[ 0 ].side.y );
}

// 2D SAT of the hull rect vs a block's XZ rect. On overlap writes the
// minimum-translation vector (axis * depth) that pushes the hull out.
bool obb_mtv( const HullObb &o, const glm::dvec3 &bmin, const glm::dvec3 &bmax, glm::dvec2 *mtv ) {
    const glm::dvec2 axes[ 4 ] = { glm::dvec2( 1.0, 0.0 ), glm::dvec2( 0.0, 1.0 ), o.fwd, o.side };
    const glm::dvec2 bc( ( bmin.x + bmax.x ) * 0.5, ( bmin.z + bmax.z ) * 0.5 );
    const glm::dvec2 be( ( bmax.x - bmin.x ) * 0.5, ( bmax.z - bmin.z ) * 0.5 );
    double best = HUGE_VAL;
    glm::dvec2 best_axis( 0.0 );
    for ( const glm::dvec2 &a : axes ) {
        const double r_h = o.hl * fabs( glm::dot( o.fwd, a ) ) + o.hw * fabs( glm::dot( o.side, a ) );
        const double r_b = be.x * fabs( a.x ) + be.y * fabs( a.y );
        const double dist = glm::dot( o.c - bc, a );
        const double overlap = r_h + r_b - fabs( dist );
        if ( overlap <= 0.0 ) {
            return false;
        }
        if ( overlap < best ) {
            best = overlap;
            best_axis = dist >= 0.0 ? a : -a;
        }
    }
    *mtv = best_axis * best;
    return true;
}

// Gathers terrain collision boxes overlapping the hull union's XZ
// bounding box at [pos.y, pos.y + height] expanded by pad on every side.
int gather_terrain( World &world, const HullObb *obbs, const int nr, const glm::dvec3 &pos, const double height,
                    const double pad, glm::dvec3 *mins, glm::dvec3 *maxs ) {
    double ex, ez;
    hull_bounds( obbs, nr, &ex, &ez );
    const glm::dvec3 amin( pos.x - ex - pad, pos.y - pad, pos.z - ez - pad );
    const glm::dvec3 amax( pos.x + ex + pad, pos.y + height + pad, pos.z + ez + pad );
    return Collision::terrain_boxes( world, amin, amax, mins, maxs, TERRAIN_MAX );
}

// Deepest terrain overlap of the hull union at (pos, yaw): the largest
// min-translation depth among (rect, terrain box) pairs whose Y range
// overlaps the hull's by more than HULL_SKIN (the block a boat rests on
// never counts as side penetration). out_mtv (optional) gets the
// winning push.
double hull_deepest( World &world, const VehicleTypeDef &def, const glm::dvec3 &pos, const float yaw, glm::dvec2 *out_mtv ) {
    HullObb obbs[ HULL_MAX_RECTS ];
    const int nr = hull_obbs( def, pos, yaw, obbs );
    glm::dvec3 mins[ TERRAIN_MAX ], maxs[ TERRAIN_MAX ];
    const int n = gather_terrain( world, obbs, nr, pos, def.height, 0.0, mins, maxs );
    double best = 0.0;
    for ( int i = 0; i < n; i++ ) {
        const double yov = std::min( pos.y + def.height, maxs[ i ].y ) - std::max( pos.y, mins[ i ].y );
        if ( yov <= HULL_SKIN ) {
            continue;
        }
        for ( int r = 0; r < nr; r++ ) {
            glm::dvec2 mtv;
            if ( obb_mtv( obbs[ r ], mins[ i ], maxs[ i ], &mtv ) && glm::length( mtv ) > best ) {
                best = glm::length( mtv );
                if ( out_mtv != nullptr ) {
                    *out_mtv = mtv;
                }
            }
        }
    }
    return best;
}

} // namespace

double vehicle_hull_penetration( World &world, const VehicleTypeDef &def, const glm::dvec3 &pos, const float yaw ) {
    return hull_deepest( world, def, pos, yaw, nullptr );
}

bool vehicle_hull_collides( World &world, const VehicleTypeDef &def, const glm::dvec3 &pos, const float yaw ) {
    // Square hulls (minecart: length <= width) behave exactly like the
    // old single-box fit check.
    if ( def.length <= def.width ) {
        return Collision::box_collides( world, vehicle_min( def, pos ), vehicle_max( def, pos ) );
    }
    return vehicle_hull_penetration( world, def, pos, yaw ) > HULL_SKIN;
}

int vehicle_hull_depenetrate( World &world, const VehicleTypeDef &def, glm::dvec3 &pos, const float yaw,
                              const double push_cap, glm::dvec2 *applied_push, glm::dvec2 normals[ 4 ] ) {
    if ( applied_push != nullptr ) {
        *applied_push = glm::dvec2( 0.0 );
    }
    int count = 0;
    double pushed = 0.0;
    for ( int pass = 0; pass < 4; pass++ ) {
        glm::dvec2 mtv;
        const double depth = hull_deepest( world, def, pos, yaw, &mtv );
        if ( depth <= HULL_SKIN ) {
            break;
        }
        glm::dvec2 push = mtv;
        const double plen = glm::length( push );
        const double room = push_cap - pushed;
        if ( plen > room ) {
            if ( room <= 0.0 ) {
                break;
            }
            push *= room / plen;
        }
        pos.x += push.x;
        pos.z += push.y;
        pushed += glm::length( push );
        if ( applied_push != nullptr ) {
            *applied_push += push;
        }
        if ( normals != nullptr && count < 4 ) {
            normals[ count++ ] = mtv / glm::length( mtv );
        }
        if ( glm::length( push ) < plen ) {
            break; // hit the cap
        }
    }
    return count;
}

double vehicle_hull_move_y( World &world, const VehicleTypeDef &def, glm::dvec3 &pos, const float yaw, double dy,
                            bool *standing ) {
    if ( dy == 0.0 ) {
        return 0.0;
    }
    HullObb obbs[ HULL_MAX_RECTS ];
    const int nr = hull_obbs( def, pos, yaw, obbs );
    // Gather terrain under the hull union's XZ box plus the Y travel range.
    glm::dvec3 mins[ TERRAIN_MAX ], maxs[ TERRAIN_MAX ];
    double ex, ez;
    hull_bounds( obbs, nr, &ex, &ez );
    const double y_lo = std::min( pos.y, pos.y + dy ) - 1e-4;
    const double y_hi = std::max( pos.y + def.height, pos.y + def.height + dy ) + 1e-4;
    const int n = Collision::terrain_boxes( world, glm::dvec3( pos.x - ex, y_lo, pos.z - ez ),
                                            glm::dvec3( pos.x + ex, y_hi, pos.z + ez ), mins, maxs, TERRAIN_MAX );
    // Only blocks some hull rect strictly overlaps in XZ count — SAT,
    // strictly positive on every axis.
    double best = dy;
    bool hit = false;
    for ( int i = 0; i < n; i++ ) {
        // Strict XZ overlap (ignore contact-skin margins).
        const double ox = std::min( pos.x + ex, maxs[ i ].x ) - std::max( pos.x - ex, mins[ i ].x );
        if ( ox <= 0.0 ) {
            continue;
        }
        bool xz_hit = false;
        for ( int r = 0; r < nr && !xz_hit; r++ ) {
            glm::dvec2 mtv;
            xz_hit = obb_mtv( obbs[ r ], mins[ i ], maxs[ i ], &mtv );
        }
        if ( !xz_hit ) {
            continue;
        }
        if ( dy < 0.0 ) {
            // Landing: the block's top face is crossed by the bottom.
            const double top = maxs[ i ].y;
            if ( top <= pos.y + HULL_SKIN && top > pos.y + dy ) {
                const double applied = top + HULL_SKIN - pos.y;
                if ( applied > best ) {
                    best = applied;
                }
                hit = true;
            }
        } else {
            const double bot = mins[ i ].y;
            if ( bot >= pos.y + def.height - HULL_SKIN && bot < pos.y + def.height + dy ) {
                const double applied = bot - def.height - HULL_SKIN - pos.y;
                if ( applied < best ) {
                    best = applied;
                }
                hit = true;
            }
        }
    }
    if ( hit && dy < 0.0 ) {
        *standing = true;
    }
    pos.y += best;
    return best;
}
