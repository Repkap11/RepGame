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

int contains_pixel( Block *pixel_block, BlockState *block_state, const glm::vec3 &dir, const glm::vec3 &initial, const glm::vec3 &block, int *which_face ) {
    // r.dir is unit direction vector of ray
    float dirfrac_x = 1.0f / dir.x;
    float dirfrac_y = 1.0f / dir.y;
    float dirfrac_z = 1.0f / dir.z;
    // lb is the corner of AABB with minimal coordinates - left bottom, rt is maximal corner
    // r.org is origin of ray

    char scale_x = pixel_block->scale.x;
    char scale_z = pixel_block->scale.z;
    char offset_x = pixel_block->offset.x;
    char offset_z = pixel_block->offset.z;
    if ( block_state->rotation == BLOCK_ROTATE_0 ) {
    } else if ( block_state->rotation == BLOCK_ROTATE_90 ) {
        scale_x = pixel_block->scale.z;
        scale_z = pixel_block->scale.x;
        offset_x = 16 - pixel_block->scale.z - pixel_block->offset.z;
        offset_z = pixel_block->offset.x;
    } else if ( block_state->rotation == BLOCK_ROTATE_180 ) {
        offset_x = 16 - pixel_block->scale.x - pixel_block->offset.x;
        offset_z = 16 - pixel_block->scale.z - pixel_block->offset.z;
    } else if ( block_state->rotation == BLOCK_ROTATE_270 ) {
        scale_x = pixel_block->scale.z;
        scale_z = pixel_block->scale.x;
        offset_x = pixel_block->offset.z;
        offset_z = 16 - pixel_block->scale.x - pixel_block->offset.x;
    }

    float c1_x = block.x + PIXEL_TO_FLOAT( offset_x );
    float c1_z = block.z + PIXEL_TO_FLOAT( offset_z );

    float c2_x = c1_x + PIXEL_TO_FLOAT( scale_x );
    float c2_z = c1_z + PIXEL_TO_FLOAT( scale_z );

    float c1_y = block.y + PIXEL_TO_FLOAT( pixel_block->offset.y );
    float c2_y = c1_y + PIXEL_TO_FLOAT( pixel_block->scale.y );

    // pr_debug( "%f %f %f", lb.x, lb.y, lb.z );
    // pr_debug( "%f %f %f", rt.x, rt.y, rt.z );
    // float length = 0;
    float all_t[ NUM_FACES_IN_CUBE ];
    all_t[ FACE_TOP ] = ( c2_y - initial.y ) * dirfrac_y;
    all_t[ FACE_BOTTOM ] = ( c1_y - initial.y ) * dirfrac_y;
    all_t[ FACE_RIGHT ] = ( c2_x - initial.x ) * dirfrac_x;
    all_t[ FACE_BACK ] = ( c2_z - initial.z ) * dirfrac_z;
    all_t[ FACE_LEFT ] = ( c1_x - initial.x ) * dirfrac_x;
    all_t[ FACE_FRONT ] = ( c1_z - initial.z ) * dirfrac_z;

    float rl_min;
    float rl_max;
    float rl_face;
    if ( all_t[ FACE_RIGHT ] < all_t[ FACE_LEFT ] ) {
        rl_face = FACE_RIGHT;
        rl_min = all_t[ FACE_RIGHT ];
        rl_max = all_t[ FACE_LEFT ];
    } else {
        rl_face = FACE_LEFT;
        rl_max = all_t[ FACE_RIGHT ];
        rl_min = all_t[ FACE_LEFT ];
    }

    float tb_min;
    float tb_max;
    float tb_face;
    if ( all_t[ FACE_TOP ] < all_t[ FACE_BOTTOM ] ) {
        tb_face = FACE_TOP;
        tb_min = all_t[ FACE_TOP ];
        tb_max = all_t[ FACE_BOTTOM ];
    } else {
        tb_face = FACE_BOTTOM;
        tb_max = all_t[ FACE_TOP ];
        tb_min = all_t[ FACE_BOTTOM ];
    }

    float fb_min;
    float fb_max;
    float fb_face;
    if ( all_t[ FACE_FRONT ] < all_t[ FACE_BACK ] ) {
        fb_face = FACE_FRONT;
        fb_min = all_t[ FACE_FRONT ];
        fb_max = all_t[ FACE_BACK ];
    } else {
        fb_face = FACE_BACK;
        fb_max = all_t[ FACE_FRONT ];
        fb_min = all_t[ FACE_BACK ];
    }

    float min;
    float face;
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
    float max = fmin( fmin( rl_max, tb_max ), fb_max );

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

int contains_block( World *world, const glm::vec3 &dir, const glm::vec3 &initial, const glm::ivec3 &block_pos, int collide_with_unloaded, int is_pick, int *which_face ) {
    BlockState blockState = world_get_loaded_block( world, block_pos );
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
        return contains_pixel( block, &blockState, dir, initial, block_pos, which_face );
    } else {
        return result;
    }
}

int ray_traversal_find_block_from_to( World *world, Block *pixel_block,               //
                                      const glm::vec3 &v1, //
                                      const glm::vec3 &v2, //
                                      glm::ivec3 &out, //
                                      int *out_whichFace, int flag, int is_pick, int is_pixel ) {

    glm::vec3 dir = v2 - v1;
    dir = glm::normalize(dir);

    //TODO do this math with glm vec types.
    const float x1 = v1.x;
    const float y1 = v1.y;
    const float z1 = v1.z;
    const float x2 = v2.x;
    const float y2 = v2.y;
    const float z2 = v2.z;


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
        int which_face = -1;
        int hit_block = contains_block( world, dir, v1, dir, flag, is_pick, &which_face );
        if ( which_face != -1 ) {
            face = which_face;
        }
        if ( hit_block ) {
            out = dir;
            if ( !flag ) {
                *out_whichFace = face;
                return 1;
            } else {
                out_whichFace[ face ] = 1;
            }
        }

        if ( tx <= ty && tx <= tz ) {
            if ( dir.x == iend )
                break;
            tx += deltatx;
            dir.x += di;
            if ( di == 1 ) {
                face = FACE_LEFT;
            }
            if ( di == -1 ) {
                face = FACE_RIGHT;
            }

        } else if ( ty <= tz ) {
            if ( dir.y == jend )
                break;
            ty += deltaty;
            dir.y += dj;
            if ( dj == 1 ) {
                face = FACE_BOTTOM;
            }
            if ( dj == -1 ) {
                face = FACE_TOP;
            }

        } else {
            if ( dir.z == kend )
                break;
            tz += deltatz;
            dir.z += dk;
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
