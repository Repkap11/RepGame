#include "utils/collision.hpp"
#include "RepGame.hpp"
#include "utils/ray_traversal.hpp"

// int ray_traversal_find_block_from_to( LoadedChunks *loadedChunks, float x1, float y1, float z1, //
//                                       float x2, float y2, float z2,                             //
//                                       int *out_x, int *out_y, int *out_z, int *out_whichFace );

int collision_check_move( LoadedChunks *loadedChunks, TRIP_ARGS( float *movement_vec_ ), TRIP_ARGS( float position_ ) ) {
    TRIP_STATE( int out_ );
    int *faces = ( int * )calloc( NUM_FACES_IN_CUBE, sizeof( int ) );
    float *faces_extra = ( float * )calloc( NUM_FACES_IN_CUBE, sizeof( float ) );
    // position_y -= 1.6f;
    int half_width_x = 0.5f;
    int half_width_y = 0.9f;
    int half_width_z = 0.5f;
    // Set of loops for block position
    for ( int offset_x = -1; offset_x < 2; offset_x++ ) {
        for ( int offset_y = -1; offset_y < 2; offset_y++ ) {
            for ( int offset_z = -1; offset_z < 2; offset_z++ ) {

                int out_face;
                float out_extra = 0;
                float new_x = position_x + offset_x * half_width_x;
                float new_y = position_y + offset_y * half_width_y - 1.6;
                float new_z = position_z + offset_z * half_width_z;

                int collide = ray_traversal_find_block_from_to( //
                    loadedChunks,                               //
                    new_x,                                      //
                    new_y,                                      //
                    new_z,                                      //
                    new_x + ( *movement_vec_x ),                //
                    new_y + ( *movement_vec_y ),                //
                    new_z + ( *movement_vec_z ),                //
                    TRIP_ARGS( &out_ ), &out_face, &out_extra );
                if ( collide ) {
                    faces[ out_face ] = 1;
                    float cur_value = faces_extra[ out_face ];
                    faces_extra[ out_face ] = out_extra < cur_value ? cur_value : out_extra;
                }
            }
        }
    }
    int collide = 0;
    // pr_debug( "New Frame" );
    if ( faces[ FACE_RIGHT ] && *movement_vec_x < 0 ) {
        *movement_vec_x = -1.0f + faces_extra[ FACE_RIGHT ];
        collide = 1;
        pr_debug( "Collide FACE_RIGHT" );
    }
    if ( faces[ FACE_LEFT ] && *movement_vec_x > 0 ) {
        *movement_vec_x = -1.0f + faces_extra[ FACE_LEFT ];
        collide = 1;

        pr_debug( "Collide FACE_LEFT" );
    }
    if ( faces[ FACE_TOP ] && *movement_vec_y < 0 ) {
        float orig_movement = *movement_vec_y;
        *movement_vec_y = -1.0f + faces_extra[ FACE_TOP ];
        if ( *movement_vec_y < -0.11f ) {
            pr_debug( "Too large" );
        }
        collide = 1;

        pr_debug( "Collide FACE_TOP" );
    }
    if ( faces[ FACE_BOTTOM ] && *movement_vec_y > 0 ) {
        *movement_vec_y = -1.0f + faces_extra[ FACE_BOTTOM ];
        collide = 1;

        pr_debug( "Collide FACE_BOTTOM" );
    }
    if ( faces[ FACE_FRONT ] && *movement_vec_z > 0 ) {
        *movement_vec_z = -1.0f + faces_extra[ FACE_FRONT ];
        collide = 1;

        pr_debug( "Collide FACE_FRONT" );
    }
    if ( faces[ FACE_BACK ] && *movement_vec_z < 0 ) {
        *movement_vec_z = -1.0f + faces_extra[ FACE_BACK ];
        collide = 1;
        pr_debug( "Collide FACE_BACK" );
    }
    if ( collide ) {
        // collision_check_move( loadedChunks, TRIP_ARGS( movement_vec_ ), TRIP_ARGS( position_ ) );
    }
    free( faces );
    free( faces_extra );
    return collide;
}