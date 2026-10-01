#include <math.h>

#include "common/RepGame.hpp"
#include "common/world.hpp"
#include "common/render_order.hpp"
#include "common/utils/ray_traversal.hpp"

// https://bitbucket.org/volumesoffun/polyvox/src/9a71004b1e72d6cf92c41da8995e21b652e6b836/include/PolyVox/Raycast.inl?at=develop&fileviewer=file-view-default

typedef struct {
    float x;
    float y;
    float z;
} RayTemp;

int contains_pixel( Block *pixel_block, BlockState &block_state, const glm::dvec3 &dir, const glm::dvec3 &initial, const glm::dvec3 &block, int *which_face ) {
    // r.dir is unit direction vector of ray
    double dirfrac_x = 1.0 / dir.x;
    double dirfrac_y = 1.0 / dir.y;
    double dirfrac_z = 1.0 / dir.z;
    // lb is the corner of AABB with minimal coordinates - left bottom, rt is maximal corner
    // r.org is origin of ray

    char scale_x = pixel_block->scale.x;
    char scale_y = pixel_block->scale.y;
    char scale_z = pixel_block->scale.z;
    char offset_x = pixel_block->offset.x;
    char offset_y = pixel_block->offset.y;
    char offset_z = pixel_block->offset.z;
    if ( pixel_block->is_piston_head ) {
        // The yaw cases below only remap x/z; piston heads also face up/down,
        // so take the whole plate box from the shared helper.
        short head_scale[ 3 ], head_offset[ 3 ];
        piston_head_shape( block_state.rotation, head_scale, head_offset );
        scale_x = head_scale[ 0 ];
        scale_y = head_scale[ 1 ];
        scale_z = head_scale[ 2 ];
        offset_x = head_offset[ 0 ];
        offset_y = head_offset[ 1 ];
        offset_z = head_offset[ 2 ];
    } else if ( block_state.rotation == BLOCK_ROTATE_0 ) {
    } else if ( block_state.rotation == BLOCK_ROTATE_90 ) {
        scale_x = pixel_block->scale.z;
        scale_z = pixel_block->scale.x;
        offset_x = 16 - pixel_block->scale.z - pixel_block->offset.z;
        offset_z = pixel_block->offset.x;
    } else if ( block_state.rotation == BLOCK_ROTATE_180 ) {
        offset_x = 16 - pixel_block->scale.x - pixel_block->offset.x;
        offset_z = 16 - pixel_block->scale.z - pixel_block->offset.z;
    } else if ( block_state.rotation == BLOCK_ROTATE_270 ) {
        scale_x = pixel_block->scale.z;
        scale_z = pixel_block->scale.x;
        offset_x = pixel_block->offset.z;
        offset_z = 16 - pixel_block->scale.x - pixel_block->offset.x;
    } else if ( block_state.rotation >= 4 ) {
        // Side-mounted torch: push bounding box towards the wall
        // rotation 4=attached LEFT, 5=attached FRONT, 6=attached RIGHT, 7=attached BACK
        if ( block_state.rotation == 4 ) {
            offset_x = 0;
        } else if ( block_state.rotation == 5 ) {
            offset_z = 16 - pixel_block->scale.z;
        } else if ( block_state.rotation == 6 ) {
            offset_x = 16 - pixel_block->scale.x;
        } else if ( block_state.rotation == 7 ) {
            offset_z = 0;
        }
    }

    double c1_x = block.x + PIXEL_TO_FLOAT( offset_x );
    double c1_z = block.z + PIXEL_TO_FLOAT( offset_z );

    double c2_x = c1_x + PIXEL_TO_FLOAT( scale_x );
    double c2_z = c1_z + PIXEL_TO_FLOAT( scale_z );

    double c1_y = block.y + PIXEL_TO_FLOAT( offset_y );
    double c2_y = c1_y + PIXEL_TO_FLOAT( scale_y );

    // pr_debug( "%f %f %f", lb.x, lb.y, lb.z );
    // pr_debug( "%f %f %f", rt.x, rt.y, rt.z );
    // float length = 0;
    double all_t[ NUM_FACES_IN_CUBE ];
    all_t[ FACE_TOP ] = ( c2_y - initial.y ) * dirfrac_y;
    all_t[ FACE_BOTTOM ] = ( c1_y - initial.y ) * dirfrac_y;
    all_t[ FACE_RIGHT ] = ( c2_x - initial.x ) * dirfrac_x;
    all_t[ FACE_BACK ] = ( c2_z - initial.z ) * dirfrac_z;
    all_t[ FACE_LEFT ] = ( c1_x - initial.x ) * dirfrac_x;
    all_t[ FACE_FRONT ] = ( c1_z - initial.z ) * dirfrac_z;

    double rl_min;
    double rl_max;
    double rl_face;
    if ( all_t[ FACE_RIGHT ] < all_t[ FACE_LEFT ] ) {
        rl_face = FACE_RIGHT;
        rl_min = all_t[ FACE_RIGHT ];
        rl_max = all_t[ FACE_LEFT ];
    } else {
        rl_face = FACE_LEFT;
        rl_max = all_t[ FACE_RIGHT ];
        rl_min = all_t[ FACE_LEFT ];
    }

    double tb_min;
    double tb_max;
    double tb_face;
    if ( all_t[ FACE_TOP ] < all_t[ FACE_BOTTOM ] ) {
        tb_face = FACE_TOP;
        tb_min = all_t[ FACE_TOP ];
        tb_max = all_t[ FACE_BOTTOM ];
    } else {
        tb_face = FACE_BOTTOM;
        tb_max = all_t[ FACE_TOP ];
        tb_min = all_t[ FACE_BOTTOM ];
    }

    double fb_min;
    double fb_max;
    double fb_face;
    if ( all_t[ FACE_FRONT ] < all_t[ FACE_BACK ] ) {
        fb_face = FACE_FRONT;
        fb_min = all_t[ FACE_FRONT ];
        fb_max = all_t[ FACE_BACK ];
    } else {
        fb_face = FACE_BACK;
        fb_max = all_t[ FACE_FRONT ];
        fb_min = all_t[ FACE_BACK ];
    }

    double min;
    double face;
    if ( rl_min > tb_min ) {
        face = rl_face;
        min = rl_min;
    } else {
        face = tb_face;
        min = tb_min;
    }
    if ( fb_min > min ) {
        face = fb_face;
        min = fb_min;
    }
    double max = fmin( fmin( rl_max, tb_max ), fb_max );

    // The whole AABB is behind us
    if ( max < 0 ) {
        // length = tmax;
        return false;
    }

    // Ray doesn't intersect AABB
    if ( min > max ) {
        // length = tmax;
        return false;
    }

    *which_face = face;
    return true;
}

int contains_block( World &world, const glm::dvec3 &dir, const glm::dvec3 &initial, const glm::ivec3 &block_pos, int collide_with_unloaded, int is_pick, int *which_face ) {
    BlockState blockState = world.get_loaded_block( block_pos );
    BlockID blockID = blockState.id;
    if ( blockID >= LAST_BLOCK_ID ) {
        return collide_with_unloaded;
    }
    Block *block = block_definition_get_definition( blockID );
    bool result;
    if ( is_pick ) {
        result = block->is_pickable;
    } else {
        result = block->collides_with_player;
    }
    if ( result && block->non_full_size ) {
        return contains_pixel( block, blockState, dir, initial, glm::dvec3( block_pos ), which_face );
    } else {
        return result;
    }
}

int RayTraversal::find_block_from_to( World &world, Block *pixel_block, //
                                      const glm::dvec3 &v1,                   //
                                      const glm::dvec3 &v2,                   //
                                      glm::ivec3 &out,                        //
                                      int *out_whichFace, int flag, int is_pick, int is_pixel ) {

    glm::dvec3 dir = v2 - v1;
    // dir = glm::normalize(dir);
    double length = sqrt( dir.x * dir.x + dir.y * dir.y + dir.z * dir.z );
    dir /= length;

    // glm::ivec3 offset = glm::floor(v1);
    glm::ivec3 offset( ( int )floor( v1.x ), ( int )floor( v1.y ), ( int )floor( v1.z ) );

    // TODO do this math with glm vec types.
    const double x1 = v1.x;
    const double y1 = v1.y;
    const double z1 = v1.z;
    const double x2 = v2.x;
    const double y2 = v2.y;
    const double z2 = v2.z;

    const int iend = ( int )floor( x2 );
    const int jend = ( int )floor( y2 );
    const int kend = ( int )floor( z2 );

    const int di = ( ( x1 < x2 ) ? 1 : ( ( x1 > x2 ) ? -1 : 0 ) );
    const int dj = ( ( y1 < y2 ) ? 1 : ( ( y1 > y2 ) ? -1 : 0 ) );
    const int dk = ( ( z1 < z2 ) ? 1 : ( ( z1 > z2 ) ? -1 : 0 ) );

    const double deltatx = 1.0 / std::abs( x2 - x1 );
    const double deltaty = 1.0 / std::abs( y2 - y1 );
    const double deltatz = 1.0 / std::abs( z2 - z1 );

    const double minx = floor( x1 ), maxx = minx + 1.0;
    double tx = ( ( x1 > x2 ) ? ( x1 - minx ) : ( maxx - x1 ) ) * deltatx;
    const double miny = floor( y1 ), maxy = miny + 1.0;
    double ty = ( ( y1 > y2 ) ? ( y1 - miny ) : ( maxy - y1 ) ) * deltaty;
    const double minz = floor( z1 ), maxz = minz + 1.0;
    double tz = ( ( z1 > z2 ) ? ( z1 - minz ) : ( maxz - z1 ) ) * deltatz;

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
        int which_face = -1;
        int hit_block = contains_block( world, dir, v1, offset, 1, is_pick, &which_face );
        if ( which_face != -1 ) {
            face = which_face;
        }
        if ( hit_block ) {
            out = offset;
            if ( !flag ) {
                *out_whichFace = face;
                return 1;
            } else {
                out_whichFace[ face ] = 1;
            }
        }

        if ( tx <= ty && tx <= tz ) {
            if ( offset.x == iend )
                break;
            tx += deltatx;
            offset.x += di;
            if ( di == 1 ) {
                face = FACE_LEFT;
            }
            if ( di == -1 ) {
                face = FACE_RIGHT;
            }

        } else if ( ty <= tz ) {
            if ( offset.y == jend )
                break;
            ty += deltaty;
            offset.y += dj;
            if ( dj == 1 ) {
                face = FACE_BOTTOM;
            }
            if ( dj == -1 ) {
                face = FACE_TOP;
            }

        } else {
            if ( offset.z == kend )
                break;
            tz += deltatz;
            offset.z += dk;
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
