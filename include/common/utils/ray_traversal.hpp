#pragma once

#include <cmath>

#include "common/chunk_loader.hpp"

class RayTraversal {
  public:
    static int find_block_from_to( World &world, Block *pixel_block, //
                                   const glm::dvec3 &v1,             //
                                   const glm::dvec3 &v2,             //
                                   glm::ivec3 &out,                  //
                                   int *out_whichFace, int flag, int is_pick, int is_pixel, int pick_water );
    // Slab test of `origin + t*dir` against the AABB [bmin, bmax]. True when
    // the ray's box entry is at t <= max_t; *out_t gets the entry distance
    // (0 when the ray starts inside the box).
    static bool ray_aabb_entry( const glm::dvec3 &origin, const glm::dvec3 &dir,
                                const glm::dvec3 &bmin, const glm::dvec3 &bmax, double max_t, double *out_t ) {
        // t0 stays >= 0: an origin inside the box reports entry distance 0
        // and a box entirely behind the ray misses (its exit t is negative).
        double t0 = 0.0, t1 = max_t;
        for ( int a = 0; a < 3; a++ ) {
            const double o = origin[ a ], d = dir[ a ];
            if ( fabs( d ) < 1e-9 ) {
                if ( o < bmin[ a ] || o > bmax[ a ] ) {
                    return false;
                }
            } else {
                double ta = ( bmin[ a ] - o ) / d, tb = ( bmax[ a ] - o ) / d;
                if ( ta > tb ) {
                    const double tmp = ta;
                    ta = tb;
                    tb = tmp;
                }
                t0 = ta > t0 ? ta : t0;
                t1 = tb < t1 ? tb : t1;
            }
        }
        if ( t0 > t1 ) {
            return false;
        }
        if ( out_t != nullptr ) {
            *out_t = t0;
        }
        return true;
    }
};