#include "utils/collision.hpp"
#include "RepGame.hpp"
#include "utils/ray_traversal.hpp"

// int ray_traversal_find_block_from_to( LoadedChunks *loadedChunks, float x1, float y1, float z1, //
//                                       float x2, float y2, float z2,                             //
//                                       int *out_x, int *out_y, int *out_z, int *out_whichFace );

int frame = 0;
void collision_check_move( LoadedChunks *loadedChunks, TRIP_ARGS( float *movement_vec_ ), TRIP_ARGS( float position_ ) ) {
    frame++;
    TRIP_STATE( int out_ );
    int *faces = ( int * )calloc( NUM_FACES_IN_CUBE, sizeof( int ) );
    // float *faces_extra = ( float * )calloc( NUM_FACES_IN_CUBE, sizeof( float ) );
    // position_y -= 1.6f;
    float half_width_x = 0.4f;
    float half_width_y = 0.4f;
    float half_width_z = 0.4f;
    // Set of loops for block position
    for ( int offset_x = -1; offset_x < 2; offset_x++ ) {
        for ( int offset_y = -1; offset_y < 2; offset_y++ ) {
            for ( int offset_z = -1; offset_z < 2; offset_z++ ) {

                int out_face;
                float new_x = position_x + offset_x * half_width_x;
                float new_y = position_y + offset_y * half_width_y; // - 1.6;
                float new_z = position_z + offset_z * half_width_z;

                int collide = ray_traversal_find_block_from_to( //
                    loadedChunks,                               //
                    new_x,                                      //
                    new_y,                                      //
                    new_z,                                      //
                    new_x + ( *movement_vec_x ),                //
                    new_y + ( *movement_vec_y ),                //
                    new_z + ( *movement_vec_z ),                //
                    TRIP_ARGS( &out_ ), faces, 1 );
                if ( collide ) {
                    // float cur_value = faces_extra[ out_face ];
                    // float next_value = ( out_extra > cur_value ) ? out_extra : cur_value;
                    // float next_value = out_extra;
                    // if ( next_value < 0.9f ) {
                    // pr_debug( "Got it %d:%d", frame, out_face );
                    // }
                    // faces_extra[ out_face ] = next_value;
                    // faces[ out_face ] = 1;
                }
            }
        }
    }
    int collide = 0;
    // pr_debug( "New Frame" );
    if ( faces[ FACE_RIGHT ] && *movement_vec_x < 0 ) {
        float movement_vec_x_orig = *movement_vec_x;
        *movement_vec_x = roundf( ( position_x - half_width_x ) + *movement_vec_x ) - ( ( position_x - half_width_x ) );
        if ( *movement_vec_x != movement_vec_x_orig ) {
            collide = 1;
        }
        pr_debug( "Collide FACE_RIGHT" );
    }
    if ( faces[ FACE_LEFT ] && *movement_vec_x > 0 ) {
        float movement_vec_x_orig = *movement_vec_x;
        *movement_vec_x = roundf( ( position_x + half_width_x ) + *movement_vec_x ) - ( ( position_x + half_width_x ) );
        if ( *movement_vec_x != movement_vec_x_orig ) {
            collide = 1;
        }

        pr_debug( "Collide FACE_LEFT" );
    }
    if ( faces[ FACE_TOP ] && *movement_vec_y < 0 ) {
        float movement_vec_y_orig = *movement_vec_y;
        float val1 = ( position_y - half_width_y ) + *movement_vec_y;
        float val2 = ( position_y - half_width_y );
        *movement_vec_y = roundf( val1 - val2 );
        if ( *movement_vec_y != movement_vec_y_orig ) {
            collide = 1;
        }
        pr_debug( "Collide FACE_TOP" );
    }
    if ( faces[ FACE_BOTTOM ] && *movement_vec_y > 0 ) {
        float movement_vec_y_orig = *movement_vec_y;
        *movement_vec_y = roundf( ( position_y + half_width_y ) + *movement_vec_y - ( position_y + half_width_y ) );
        if ( *movement_vec_y != movement_vec_y_orig ) {
            collide = 1;
        }
        pr_debug( "Collide FACE_BOTTOM" );
    }
    if ( faces[ FACE_FRONT ] && *movement_vec_z < 0 ) {
        float movement_vec_z_orig = *movement_vec_z;
        *movement_vec_z = roundf( ( position_z - half_width_z ) + *movement_vec_z ) - ( ( position_z - half_width_z ) );
        if ( *movement_vec_z != movement_vec_z_orig ) {
            collide = 1;
        }
        pr_debug( "Collide FACE_FRONT" );
    }
    if ( faces[ FACE_BACK ] && *movement_vec_z < 0 ) {
        float movement_vec_z_orig = *movement_vec_z;
        *movement_vec_z = roundf( ( position_z + half_width_z ) + *movement_vec_z ) - ( ( position_z + half_width_z ) );
        if ( *movement_vec_z != movement_vec_z_orig ) {
            collide = 1;
        }
        pr_debug( "Collide FACE_BACK" );
    }
    free( faces );
    if ( collide ) {
        collision_check_move( loadedChunks, TRIP_ARGS( movement_vec_ ), TRIP_ARGS( position_ ) );
    }
}