#include <math.h>

#include "common/RepGame.hpp"
#include "common/world.hpp"
#include "common/render_order.hpp"

// https://bitbucket.org/volumesoffun/polyvox/src/9a71004b1e72d6cf92c41da8995e21b652e6b836/include/PolyVox/Raycast.inl?at=develop&fileviewer=file-view-default

int contains_block( LoadedChunks *gameChunks, int block_x, int block_y, int block_z, int collide_with_unloaded, int is_pick ) {
    BlockID blockID = world_get_loaded_block( gameChunks, TRIP_ARGS( block_ ) );
    if ( blockID >= LAST_BLOCK_ID ) {
        return collide_with_unloaded;
    }
    Block *block = block_definition_get_definition( blockID );
    if ( is_pick ) {
        return render_order_is_pickable( block->renderOrder );
    } else {
        return render_order_collides_with_player( block->renderOrder );
    }
}

int ray_traversal_find_block_from_to( LoadedChunks *gameChunks, const float x1, const float y1, const float z1, //
                                      const float x2, const float y2, const float z2,                           //
                                      int *out_x, int *out_y, int *out_z, int *out_whichFace, int flag, int is_pick ) {

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
        if ( contains_block( gameChunks, i, j, k, flag, is_pick ) ) {
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
