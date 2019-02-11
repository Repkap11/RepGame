#include "common/utils/collision.hpp"
#include "common/RepGame.hpp"
#include "common/utils/ray_traversal.hpp"

void check_collides_with_player( LoadedChunks *loadedChunks, TRIP_ARGS( float *movement_vec_ ), TRIP_ARGS( float position_ ) ) {
    float half_width_x = PLAYER_WIDTH / 2.0f;
    float half_width_y = PLAYER_HEIGHT / 2.0f;
    float half_width_z = PLAYER_WIDTH / 2.0f;
    int out_x;
    int out_y;
    int out_z;
    int *faces = ( int * )calloc( NUM_FACES_IN_CUBE, sizeof( int ) );
    position_y -= EYE_POSITION_OFFSET;

    // For each point in the persons collision box, check the direction
    for ( int offset_x = -1; offset_x < 2; offset_x++ ) {
        for ( int offset_y = -1; offset_y < 2; offset_y++ ) {
            for ( int offset_z = -1; offset_z < 2; offset_z++ ) {
                float new_x = position_x + offset_x * half_width_x;
                float new_y = position_y + offset_y * half_width_y;
                float new_z = position_z + offset_z * half_width_z;

                ray_traversal_find_block_from_to( //
                    loadedChunks,                 //
                    new_x,                        //
                    new_y,                        //
                    new_z,                        //
                    new_x + ( *movement_vec_x ),  //
                    new_y + ( *movement_vec_y ),  //
                    new_z + ( *movement_vec_z ),  //
                    TRIP_ARGS( &out_ ), faces, 1 );
            }
        }
    }
    int collide = 0;
    if ( faces[ FACE_RIGHT ] && *movement_vec_x < 0 ) {
        float movement_vec_x_orig = *movement_vec_x;
        *movement_vec_x = roundf( ( position_x - half_width_x ) + *movement_vec_x ) - ( position_x - half_width_x );
        if ( *movement_vec_x != movement_vec_x_orig ) {
            *movement_vec_x += .00001f;
        }
    }
    if ( faces[ FACE_LEFT ] && *movement_vec_x > 0 ) {
        float movement_vec_x_orig = *movement_vec_x;
        *movement_vec_x = roundf( ( position_x + half_width_x ) + *movement_vec_x ) - ( position_x + half_width_x );
        if ( *movement_vec_x != movement_vec_x_orig ) {
            *movement_vec_x -= .00001f;
        }
    }
    if ( faces[ FACE_TOP ] && *movement_vec_y < 0 ) {
        float movement_vec_y_orig = *movement_vec_y;
        *movement_vec_y = roundf( ( position_y - half_width_y ) + *movement_vec_y ) - ( position_y - half_width_y );
        if ( *movement_vec_y != movement_vec_y_orig ) {
            *movement_vec_y += .00001f;
        }
    }
    if ( faces[ FACE_BOTTOM ] && *movement_vec_y > 0 ) {
        float movement_vec_y_orig = *movement_vec_y;
        *movement_vec_y = roundf( ( position_y + half_width_y ) + *movement_vec_y ) - ( position_y + half_width_y );
        if ( *movement_vec_y != movement_vec_y_orig ) {
            *movement_vec_y -= .00001f;
        }
    }
    if ( faces[ FACE_BACK ] && *movement_vec_z < 0 ) {
        float movement_vec_z_orig = *movement_vec_z;
        *movement_vec_z = roundf( ( position_z - half_width_z ) + *movement_vec_z ) - ( position_z - half_width_z );
        if ( *movement_vec_z != movement_vec_z_orig ) {
            *movement_vec_z += .00001f;
        }
    }
    if ( faces[ FACE_FRONT ] && *movement_vec_z > 0 ) {
        float movement_vec_z_orig = *movement_vec_z;
        *movement_vec_z = roundf( ( position_z + half_width_z ) + *movement_vec_z ) - ( position_z + half_width_z );
        if ( *movement_vec_z != movement_vec_z_orig ) {
            *movement_vec_z -= .00001f;
        }
    }
    free( faces );
}

void collision_check_move( LoadedChunks *loadedChunks, TRIP_ARGS( float *movement_vec_ ), TRIP_ARGS( float position_ ) ) {
    int *dirs_to_check = ( int * )calloc( NUM_FACES_IN_CUBE, sizeof( int ) );
    if ( *movement_vec_x > 0 ) {
        dirs_to_check[ FACE_RIGHT ] = 1;
    }
    if ( *movement_vec_x < 0 ) {
        dirs_to_check[ FACE_LEFT ] = 1;
    }
    if ( *movement_vec_y > 0 ) {
        dirs_to_check[ FACE_TOP ] = 1;
    }
    if ( *movement_vec_y < 0 ) {
        dirs_to_check[ FACE_BOTTOM ] = 1;
    }
    if ( *movement_vec_z > 0 ) {
        dirs_to_check[ FACE_FRONT ] = 1;
    }
    if ( *movement_vec_z < 0 ) {
        dirs_to_check[ FACE_BACK ] = 1;
    }
    // Check for collides in each direction
    float zero = 0;
    if ( dirs_to_check[ FACE_RIGHT ] ) {
        check_collides_with_player( loadedChunks, movement_vec_x, &zero, &zero, TRIP_ARGS( position_ ) );
    }
    if ( dirs_to_check[ FACE_LEFT ] ) {
        check_collides_with_player( loadedChunks, movement_vec_x, &zero, &zero, TRIP_ARGS( position_ ) );
    }
    if ( dirs_to_check[ FACE_TOP ] ) {
        check_collides_with_player( loadedChunks, &zero, movement_vec_y, &zero, TRIP_ARGS( position_ ) );
    }
    if ( dirs_to_check[ FACE_BOTTOM ] ) {
        check_collides_with_player( loadedChunks, &zero, movement_vec_y, &zero, TRIP_ARGS( position_ ) );
    }
    if ( dirs_to_check[ FACE_FRONT ] ) {
        check_collides_with_player( loadedChunks, &zero, &zero, movement_vec_z, TRIP_ARGS( position_ ) );
    }
    if ( dirs_to_check[ FACE_BACK ] ) {
        check_collides_with_player( loadedChunks, &zero, &zero, movement_vec_z, TRIP_ARGS( position_ ) );
    }
    free( dirs_to_check );
}