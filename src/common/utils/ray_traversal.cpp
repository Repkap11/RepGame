#include <math.h>

#include "common/RepGame.hpp"
#include "common/world.hpp"
#include "common/render_order.hpp"

// https://bitbucket.org/volumesoffun/polyvox/src/9a71004b1e72d6cf92c41da8995e21b652e6b836/include/PolyVox/Raycast.inl?at=develop&fileviewer=file-view-default

typedef struct {
    float x;
    float y;
    float z;
} RayTemp;

int contains_pixel( Block *pixel_block, float dir_x, float dir_y, float dir_z, float initial_x, float initial_y, float initial_z, float block_x, float block_y, float block_z ) {
    // r.dir is unit direction vector of ray
    float dirfrac_x = 1.0f / dir_x;
    float dirfrac_y = 1.0f / dir_y;
    float dirfrac_z = 1.0f / dir_z;
    // lb is the corner of AABB with minimal coordinates - left bottom, rt is maximal corner
    // r.org is origin of ray
    RayTemp lb = {0, 0, 0};
    RayTemp rt = {1, 0.5f, 1};
    float c1_x = block_x + pixel_block->offset.x;
    float c1_y = block_y + pixel_block->offset.y;
    float c1_z = block_z + pixel_block->offset.z;

    float c2_x = c1_x + pixel_block->scale.x;
    float c2_y = c1_y + pixel_block->scale.y;
    float c2_z = c1_z + pixel_block->scale.z;

    // pr_debug( "%f %f %f", lb.x, lb.y, lb.z );
    // pr_debug( "%f %f %f", rt.x, rt.y, rt.z );
    float length = 0;
    float t1 = ( c1_x - initial_x ) * dirfrac_x;
    float t2 = ( c2_x - initial_x ) * dirfrac_x;
    float t3 = ( c1_y - initial_y ) * dirfrac_y;
    float t4 = ( c2_y - initial_y ) * dirfrac_y;
    float t5 = ( c1_z - initial_z ) * dirfrac_z;
    float t6 = ( c2_z - initial_z ) * dirfrac_z;

    float tmin = fmax( fmax( fmin( t1, t2 ), fmin( t3, t4 ) ), fmin( t5, t6 ) );
    float tmax = fmin( fmin( fmax( t1, t2 ), fmax( t3, t4 ) ), fmax( t5, t6 ) );

    pr_debug( "min:%f max:%f", tmin, tmax );

    // if tmax < 0, ray (line) is intersecting AABB, but the whole AABB is behind us
    if ( tmax < 0 ) {
        length = tmax;
        return false;
    }

    // if tmin > tmax, ray doesn't intersect AABB
    if ( tmin > tmax ) {
        length = tmax;
        return false;
    }

    length = tmin;
    return true;
}

int contains_block( World *world, float dir_x, float dir_y, float dir_z, float initial_x, float initial_y, float initial_z, int block_x, int block_y, int block_z, int collide_with_unloaded, int is_pick ) {
    BlockID blockID = world_get_loaded_block( world, TRIP_ARGS( block_ ) ).id;
    if ( blockID >= LAST_BLOCK_ID ) {
        return collide_with_unloaded;
    }
    Block *block = block_definition_get_definition( blockID );
    bool result;
    if ( is_pick ) {
        result = render_order_is_pickable( block->renderOrder );
    } else {
        result = render_order_collides_with_player( block->renderOrder );
    }
    if ( result && block->non_full_size ) {
        return contains_pixel( block, dir_x, dir_y, dir_z, initial_x, initial_y, initial_z, block_x, block_y, block_z );
    } else {
        return result;
    }
}

int ray_traversal_find_block_from_to( World *world, Block *pixel_block,               //
                                      const float x1, const float y1, const float z1, //
                                      const float x2, const float y2, const float z2, //
                                      int *out_x, int *out_y, int *out_z,             //
                                      int *out_whichFace, int flag, int is_pick, int is_pixel ) {

    float dir_x = x2 - x1;
    float dir_y = y2 - y1;
    float dir_z = z2 - z1;
    float length = sqrtf( dir_x * dir_x + dir_y * dir_y + dir_z * dir_z );
    dir_x /= length;
    dir_y /= length;
    dir_z /= length;

    int i = ( int )floorf( x1 );
    int j = ( int )floorf( y1 );
    int k = ( int )floorf( z1 );

    const int iend = ( int )floorf( x2 );
    const int jend = ( int )floorf( y2 );
    const int kend = ( int )floorf( z2 );

    const int di = ( ( x1 < x2 ) ? 1 : ( ( x1 > x2 ) ? -1 : 0 ) );
    const int dj = ( ( y1 < y2 ) ? 1 : ( ( y1 > y2 ) ? -1 : 0 ) );
    const int dk = ( ( z1 < z2 ) ? 1 : ( ( z1 > z2 ) ? -1 : 0 ) );

    const float deltatx = 1.0f / std::abs( x2 - x1 );
    const float deltaty = 1.0f / std::abs( y2 - y1 );
    const float deltatz = 1.0f / std::abs( z2 - z1 );

    const float minx = floorf( x1 ), maxx = minx + 1.0f;
    float tx = ( ( x1 > x2 ) ? ( x1 - minx ) : ( maxx - x1 ) ) * deltatx;
    const float miny = floorf( y1 ), maxy = miny + 1.0f;
    float ty = ( ( y1 > y2 ) ? ( y1 - miny ) : ( maxy - y1 ) ) * deltaty;
    const float minz = floorf( z1 ), maxz = minz + 1.0f;
    float tz = ( ( z1 > z2 ) ? ( z1 - minz ) : ( maxz - z1 ) ) * deltatz;

    int face = FACE_TOP;
    if ( di == 1 ) {
        face = FACE_LEFT;
    }
    if ( di == -1 ) {
        face = FACE_RIGHT;
    }
    if ( dj == 1 ) {
        face = FACE_BOTTOM;
    }
    if ( dj == -1 ) {
        face = FACE_TOP;
    }
    if ( dk == 1 ) {
        face = FACE_FRONT;
    }
    if ( dk == -1 ) {
        face = FACE_BACK;
    }
    for ( ;; ) {
        if ( contains_block( world, dir_x, dir_y, dir_z, x1, y1, z1, i, j, k, flag, is_pick ) ) {
            *out_x = i;
            *out_y = j;
            *out_z = k;
            if ( !flag ) {
                *out_whichFace = face;
                return 1;
            } else {
                out_whichFace[ face ] = 1;
            }
        }

        if ( tx <= ty && tx <= tz ) {
            if ( i == iend )
                break;
            tx += deltatx;
            i += di;
            if ( di == 1 ) {
                face = FACE_LEFT;
            }
            if ( di == -1 ) {
                face = FACE_RIGHT;
            }

        } else if ( ty <= tz ) {
            if ( j == jend )
                break;
            ty += deltaty;
            j += dj;
            if ( dj == 1 ) {
                face = FACE_BOTTOM;
            }
            if ( dj == -1 ) {
                face = FACE_TOP;
            }

        } else {
            if ( k == kend )
                break;
            tz += deltatz;
            k += dk;
            if ( dk == 1 ) {
                face = FACE_FRONT;
            }
            if ( dk == -1 ) {
                face = FACE_BACK;
            }
        }
    }
    return 0;
}
