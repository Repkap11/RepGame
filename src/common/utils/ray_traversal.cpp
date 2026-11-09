#include <math.h>

#include "common/RepGame.hpp"
#include "common/fluid.hpp"
#include "common/world.hpp"
#include "common/utils/ray_traversal.hpp"

// https://bitbucket.org/volumesoffun/polyvox/src/9a71004b1e72d6cf92c41da8995e21b652e6b836/include/PolyVox/Raycast.inl?at=develop&fileviewer=file-view-default

typedef struct {
    float x;
    float y;
    float z;
} RayTemp;

// Slab test of `initial + t*dir` against a world-space AABB. On hit reports
// the entry face in this file's convention (LEFT/RIGHT = the -x/+x plane,
// BOTTOM/TOP = -y/+y, FRONT/BACK = -z/+z — matching contains_pixel's all_t
// indexing; the z names are opposite the mesh FACE_* table) and the entry
// distance t (0 when the origin is already inside).
static bool ray_aabb_hit( const glm::dvec3 &initial, const glm::dvec3 &dir, const glm::dvec3 &bmin, const glm::dvec3 &bmax, int *out_face, double *out_t ) {
    static const int lo_face[ 3 ] = { FACE_LEFT, FACE_BOTTOM, FACE_FRONT };
    static const int hi_face[ 3 ] = { FACE_RIGHT, FACE_TOP, FACE_BACK };
    double tmin = -1e30, tmax = 1e30;
    int face = -1;
    for ( int a = 0; a < 3; a++ ) {
        const double o = initial[ a ], d = dir[ a ];
        if ( fabs( d ) < 1e-9 ) {
            if ( o < bmin[ a ] || o > bmax[ a ] ) {
                return false;
            }
            continue;
        }
        double ta = ( bmin[ a ] - o ) / d;
        double tb = ( bmax[ a ] - o ) / d;
        int f = lo_face[ a ];
        if ( ta > tb ) {
            const double tmp = ta;
            ta = tb;
            tb = tmp;
            f = hi_face[ a ];
        }
        if ( ta > tmin ) {
            tmin = ta;
            face = f;
        }
        if ( tb < tmax ) {
            tmax = tb;
        }
    }
    if ( tmax < 0.0 || tmin > tmax || face < 0 ) {
        return false;
    }
    *out_face = face;
    *out_t = tmin < 0.0 ? 0.0 : tmin;
    return true;
}

static int contains_pixel( Block *pixel_block, BlockState &block_state, const glm::dvec3 &dir, const glm::dvec3 &initial, const glm::dvec3 &block, int *which_face, double *out_t, double max_t ) {
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
    // Fence gates mask their open bit out of the yaw — an open gate
    // (rotation 4-7) must never reach the wall-torch branch below.
    const unsigned char rot = block_yaw( pixel_block, block_state.rotation );
    if ( pixel_block->is_piston_head ) {
        // The yaw cases below only remap x/z; piston heads also face up/down,
        // so take the whole plate box from the shared helper.
        short head_scale[ 3 ], head_offset[ 3 ];
        piston_head_shape( rot, head_scale, head_offset );
        scale_x = head_scale[ 0 ];
        scale_y = head_scale[ 1 ];
        scale_z = head_scale[ 2 ];
        offset_x = head_offset[ 0 ];
        offset_y = head_offset[ 1 ];
        offset_z = head_offset[ 2 ];
    } else if ( pixel_block->is_piston && block_state.current_redstone_power > 0 ) {
        // Extended base: full cube minus the 4px slab at the head end.
        short base_scale[ 3 ], base_offset[ 3 ];
        piston_base_shape( rot, base_scale, base_offset );
        scale_x = base_scale[ 0 ];
        scale_y = base_scale[ 1 ];
        scale_z = base_scale[ 2 ];
        offset_x = base_offset[ 0 ];
        offset_y = base_offset[ 1 ];
        offset_z = base_offset[ 2 ];
    } else if ( pixel_block->is_torch ) {
        // Torches pick on a 4px-wide box (easier to hit than the 2px
        // stick). Before the rot==0 case: floor torches are rotation 0.
        short torch_scale[ 3 ], torch_offset[ 3 ];
        torch_pick_shape( pixel_block, rot, torch_scale, torch_offset );
        scale_x = torch_scale[ 0 ];
        scale_y = torch_scale[ 1 ];
        scale_z = torch_scale[ 2 ];
        offset_x = torch_offset[ 0 ];
        offset_y = torch_offset[ 1 ];
        offset_z = torch_offset[ 2 ];
    } else if ( rot == BLOCK_ROTATE_0 ) {
    } else if ( rot == BLOCK_ROTATE_90 ) {
        scale_x = pixel_block->scale.z;
        scale_z = pixel_block->scale.x;
        offset_x = 16 - pixel_block->scale.z - pixel_block->offset.z;
        offset_z = pixel_block->offset.x;
    } else if ( rot == BLOCK_ROTATE_180 ) {
        offset_x = 16 - pixel_block->scale.x - pixel_block->offset.x;
        offset_z = 16 - pixel_block->scale.z - pixel_block->offset.z;
    } else if ( rot == BLOCK_ROTATE_270 ) {
        scale_x = pixel_block->scale.z;
        scale_z = pixel_block->scale.x;
        offset_x = pixel_block->offset.z;
        offset_z = 16 - pixel_block->scale.x - pixel_block->offset.x;
    } else if ( rot >= 4 ) {
        // Side-mounted torch: push bounding box towards the wall
        // rotation 4=attached LEFT, 5=attached FRONT, 6=attached RIGHT, 7=attached BACK
        if ( rot == 4 ) {
            offset_x = 0;
        } else if ( rot == 5 ) {
            offset_z = 16 - pixel_block->scale.z;
        } else if ( rot == 6 ) {
            offset_x = 16 - pixel_block->scale.x;
        } else if ( rot == 7 ) {
            offset_z = 0;
        }
    }
    if ( pixel_block->is_slab && ( block_state.rotation & BLOCK_ROTATE_SLAB_TOP ) != 0 ) {
        // Top-half slab: the pick box mirrors into the upper half of the cell.
        offset_y = 16 - scale_y - offset_y;
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

    // The box entry is past the end of the ray segment.
    if ( min > max_t ) {
        return false;
    }

    *which_face = face;
    if ( out_t != nullptr ) {
        *out_t = min < 0.0 ? 0.0 : min;
    }
    return true;
}

static int contains_block( World &world, const glm::dvec3 &dir, const glm::dvec3 &initial, const glm::ivec3 &block_pos, int pick_mode, int *which_face, double *out_t, double max_t ) {
    BlockState blockState = world.get_loaded_block( block_pos );
    BlockID blockID = blockState.id;
    if ( blockID >= LAST_BLOCK_ID ) {
        return 1;
    }
    Block *block = block_definition_get_definition( blockID );
    // Fluid pick modes: 1 floats on WATER cells (lily pads), 2 hits any
    // fluid cell (a filled bucket aims its pour at the flow itself), 3
    // hits only source cells — MC's SOURCE_ONLY, so flowing cells are
    // transparent and can't shadow the scoopable source behind them.
    const bool fluid_pick = pick_mode == 1   ? blockID == WATER
                          : pick_mode == 2   ? block_is_fluid( block )
                          : pick_mode == 3   ? block_is_fluid( block ) && fluid_level( blockState ) == 0
                                             : false;
    const bool result = block->is_pickable || block->is_door || block_is_tall_plant( block ) || fluid_pick;
    if ( result && block->is_stairs ) {
        // Pick against the stair's real collision boxes (slab + step, with
        // the neighbor-derived corner shape) — the nearest box hit wins.
        static const glm::ivec3 stair_dirs[ 4 ] = {
            glm::ivec3( 0, 0, -1 ), glm::ivec3( 1, 0, 0 ), glm::ivec3( 0, 0, 1 ), glm::ivec3( -1, 0, 0 ),
        };
        int stair_rots[ 4 ];
        for ( int d = 0; d < 4; d++ ) {
            const BlockState neighbor = world.get_loaded_block( block_pos + stair_dirs[ d ] );
            stair_rots[ d ] = ( neighbor.id < LAST_BLOCK_ID && block_definition_get_definition( neighbor.id )->is_stairs ) ? neighbor.rotation : -1;
        }
        BlockBox boxes[ STAIRS_MAX_BOXES ];
        const int num_boxes = stairs_collision_boxes( blockState.rotation, stair_rots, boxes );
        double best_t = 1e30;
        int best_face = -1;
        for ( int i = 0; i < num_boxes; i++ ) {
            const glm::dvec3 bmin = glm::dvec3( block_pos ) + glm::dvec3( PIXEL_TO_FLOAT( boxes[ i ].offset[ 0 ] ), PIXEL_TO_FLOAT( boxes[ i ].offset[ 1 ] ), PIXEL_TO_FLOAT( boxes[ i ].offset[ 2 ] ) );
            const glm::dvec3 bmax = bmin + glm::dvec3( PIXEL_TO_FLOAT( boxes[ i ].scale[ 0 ] ), PIXEL_TO_FLOAT( boxes[ i ].scale[ 1 ] ), PIXEL_TO_FLOAT( boxes[ i ].scale[ 2 ] ) );
            int face = -1;
            double t = 0.0;
            if ( ray_aabb_hit( initial, dir, bmin, bmax, &face, &t ) && t <= max_t && t < best_t ) {
                best_t = t;
                best_face = face;
            }
        }
        if ( best_face < 0 ) {
            return 0;
        }
        *which_face = best_face;
        if ( out_t != nullptr ) {
            *out_t = best_t;
        }
        return 1;
    }
    if ( result && ( block->non_full_size || ( block->is_piston && blockState.current_redstone_power > 0 ) ) ) {
        return contains_pixel( block, blockState, dir, initial, glm::dvec3( block_pos ), which_face, out_t, max_t );
    } else {
        return result;
    }
}

// Cell on the other side of `face` from `hit` — the placement position for
// a ray hit. This traversal's face convention has FRONT on the cell's -z
// side and BACK on +z, so z maps as +(BACK)-(FRONT); do NOT substitute
// FACE_DIR_*_OFFSETS (mesh_gen_shared.hpp), whose z sign is opposite
// (FRONT=+1, BACK=-1) — swapping it in would flip z-face placement.
glm::ivec3 RayTraversal::placement_cell( const glm::ivec3 &hit, int face ) {
    glm::ivec3 out = hit;
    out.x += ( face == FACE_RIGHT ) - ( face == FACE_LEFT );
    out.y += ( face == FACE_TOP ) - ( face == FACE_BOTTOM );
    out.z += ( face == FACE_BACK ) - ( face == FACE_FRONT );
    return out;
}

// Distance t along dir at which the ray entered `cell` through `face` —
// used for full-cube hits (box hits report their own entry t). face names
// use this file's convention: LEFT/RIGHT = -x/+x plane, FRONT/BACK = -z/+z.
static double boundary_entry_t( int face, const glm::ivec3 &cell, const glm::dvec3 &from, const glm::dvec3 &dir ) {
    int axis;
    double plane;
    switch ( face ) {
    case FACE_LEFT:
        axis = 0;
        plane = cell.x;
        break;
    case FACE_RIGHT:
        axis = 0;
        plane = cell.x + 1;
        break;
    case FACE_BOTTOM:
        axis = 1;
        plane = cell.y;
        break;
    case FACE_TOP:
        axis = 1;
        plane = cell.y + 1;
        break;
    case FACE_FRONT:
        axis = 2;
        plane = cell.z;
        break;
    case FACE_BACK:
        axis = 2;
        plane = cell.z + 1;
        break;
    default:
        return 0.0;
    }
    if ( fabs( dir[ axis ] ) < 1e-9 ) {
        return 0.0;
    }
    const double t = ( plane - from[ axis ] ) / dir[ axis ];
    return t < 0.0 ? 0.0 : t;
}

bool RayTraversal::find_block_from_to( World &world, const glm::dvec3 &from, const glm::dvec3 &to, glm::ivec3 &out, int *out_face, int pick_mode, double *out_hit_t ) {

    glm::dvec3 dir = to - from;
    // dir = glm::normalize(dir);
    double length = sqrt( dir.x * dir.x + dir.y * dir.y + dir.z * dir.z );
    dir /= length;

    // glm::ivec3 offset = glm::floor(from);
    glm::ivec3 offset( ( int )floor( from.x ), ( int )floor( from.y ), ( int )floor( from.z ) );

    // TODO do this math with glm vec types.
    const double x1 = from.x;
    const double y1 = from.y;
    const double z1 = from.z;
    const double x2 = to.x;
    const double y2 = to.y;
    const double z2 = to.z;

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
        double box_t = -1.0;
        int hit_block = contains_block( world, dir, from, offset, pick_mode, &which_face, &box_t, length );
        if ( which_face != -1 ) {
            face = which_face;
        }
        if ( hit_block ) {
            out = offset;
            *out_face = face;
            if ( out_hit_t != nullptr ) {
                *out_hit_t = which_face != -1 ? box_t : boundary_entry_t( face, out, from, dir );
            }
            return true;
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
    return false;
}
