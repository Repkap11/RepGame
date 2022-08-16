#include "common/utils/collision.hpp"
#include "common/RepGame.hpp"
#include "common/utils/ray_traversal.hpp"

float half_width_x = ( PLAYER_WIDTH / 2.0f );
float half_width_y = ( PLAYER_HEIGHT / 2.0f );
float half_width_z = ( PLAYER_WIDTH / 2.0f );

int collision_check_collides_with_block( World *world, const glm::vec3 &player, const glm::vec3 &block ) {
    float player_y = player.y - EYE_POSITION_OFFSET;
    // For each point in the persons collision box, check the direction
    for ( int offset_x = -1; offset_x < 2; offset_x++ ) {
        for ( int offset_y = -1; offset_y < 2; offset_y++ ) {
            for ( int offset_z = -1; offset_z < 2; offset_z++ ) {
                int new_x = ( int )floorf( player.x + offset_x * half_width_x );
                int new_y = ( int )floorf( player_y + offset_y * half_width_y );
                int new_z = ( int )floorf( player.z + offset_z * half_width_z );

                if ( block.x == new_x && block.y == new_y && block.z == new_z ) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

void check_collides_with_player( World *world, glm::vec3 &movement_vec, glm::vec3 &position ) {
    glm::ivec3 out_pos;
    int *faces = ( int * )calloc( NUM_FACES_IN_CUBE, sizeof( int ) );
    float position_y = position.y - EYE_POSITION_OFFSET;

    // For each point in the persons collision box, check the direction
    for ( int offset_x = -1; offset_x < 2; offset_x++ ) {
        for ( int offset_y = -1; offset_y < 2; offset_y++ ) {
            for ( int offset_z = -1; offset_z < 2; offset_z++ ) {
                float half_off_x = offset_x * half_width_x;
                float half_off_y = offset_y * half_width_y;
                float half_off_z = offset_z * half_width_z;
                glm::vec3 new_1 = position + glm::vec3( half_off_x, half_off_y - EYE_POSITION_OFFSET, half_off_z );
                glm::vec3 new_2 = new_1 + movement_vec;

                ray_traversal_find_block_from_to( //
                    world,                        //
                    NULL,                         //
                    new_1,                        //
                    new_2,                        //
                    out_pos,                      //
                    faces, 1, 0, 0 );
            }
        }
    }
    if ( faces[ FACE_RIGHT ] && movement_vec.x < 0 ) {
        float movement_vec_x_orig = movement_vec.x;
        movement_vec.x = roundf( ( position.x - half_width_x ) + movement_vec.x ) - ( position.x - half_width_x );
        if ( movement_vec.x != movement_vec_x_orig ) {
            movement_vec.x += .00001f;
        }
    }
    if ( faces[ FACE_LEFT ] && movement_vec.x > 0 ) {
        float movement_vec_x_orig = movement_vec.x;
        movement_vec.x = roundf( ( position.x + half_width_x ) + movement_vec.x ) - ( position.x + half_width_x );
        if ( movement_vec.x != movement_vec_x_orig ) {
            movement_vec.x -= .00001f;
        }
    }
    if ( faces[ FACE_TOP ] && movement_vec.y < 0 ) {
        pr_debug("Rounding top");
        float movement_vec_y_orig = movement_vec.y;
        movement_vec.y = roundf( ( position_y - half_width_y ) + movement_vec.y ) - ( position_y - half_width_y );
        if ( movement_vec.y != movement_vec_y_orig ) {
            movement_vec.y += .00001f;
        }
    }
    if ( faces[ FACE_BOTTOM ] && movement_vec.y > 0 ) {
        pr_debug("Rounding bottom");
        float movement_vec_y_orig = movement_vec.y;
        movement_vec.y = roundf( ( position_y + half_width_y ) + movement_vec.y ) - ( position_y + half_width_y );
        if ( movement_vec.y != movement_vec_y_orig ) {
            movement_vec.y -= .00001f;
        }
    }
    if ( faces[ FACE_BACK ] && movement_vec.z < 0 ) {
        float movement_vec_z_orig = movement_vec.z;
        movement_vec.z = roundf( ( position.z - half_width_z ) + movement_vec.z ) - ( position.z - half_width_z );
        if ( movement_vec.z != movement_vec_z_orig ) {
            movement_vec.z += .00001f;
        }
    }
    if ( faces[ FACE_FRONT ] && movement_vec.z > 0 ) {
        float movement_vec_z_orig = movement_vec.z;
        movement_vec.z = roundf( ( position.z + half_width_z ) + movement_vec.z ) - ( position.z + half_width_z );
        if ( movement_vec.z != movement_vec_z_orig ) {
            movement_vec.z -= .00001f;
        }
    }
    free( faces );
}

void collision_check_move( World *world, glm::vec3 &movement_vec, glm::vec3 &position, int *out_standing ) {
    int standing = 0;
    int *dirs_to_check = ( int * )calloc( NUM_FACES_IN_CUBE, sizeof( int ) );
    if ( movement_vec.x > 0 ) {
        dirs_to_check[ FACE_RIGHT ] = 1;
    }
    if ( movement_vec.x < 0 ) {
        dirs_to_check[ FACE_LEFT ] = 1;
    }
    if ( movement_vec.y > 0 ) {
        dirs_to_check[ FACE_TOP ] = 1;
    }
    if ( movement_vec.y < 0 ) {
        dirs_to_check[ FACE_BOTTOM ] = 1;
    }
    if ( movement_vec.z > 0 ) {
        dirs_to_check[ FACE_FRONT ] = 1;
    }
    if ( movement_vec.z < 0 ) {
        dirs_to_check[ FACE_BACK ] = 1;
    }
    // Check for collides in each direction
    float zero = 0;
    if ( dirs_to_check[ FACE_RIGHT ] ) {
        glm::vec3 temp( movement_vec.x, 0, 0 );
        check_collides_with_player( world, temp, position );
        movement_vec.x = temp.x;
    }
    if ( dirs_to_check[ FACE_LEFT ] ) {
        glm::vec3 temp(movement_vec.x, 0, 0);
        check_collides_with_player( world, temp, position );
        movement_vec.x = temp.x;
    }
    if ( dirs_to_check[ FACE_TOP ] ) {
        pr_debug("Checking top");
        glm::vec3 temp(0, movement_vec.y, 0);
        check_collides_with_player( world, temp, position );
        movement_vec.y = temp.y;
    }
    if ( dirs_to_check[ FACE_BOTTOM ] ) {
        pr_debug("Checking bottom");
        float before = movement_vec.y;
        glm::vec3 temp( 0, movement_vec.y, 0);
        check_collides_with_player( world, temp, position );
        movement_vec.y = temp.y;

        if ( before != movement_vec.y ) {
            standing = 1;
        }
    }
    pr_debug("Move Y:%f", movement_vec.y)
    if ( dirs_to_check[ FACE_FRONT ] ) {
        glm::vec3 temp( 0, 0, movement_vec.z);
        check_collides_with_player( world, temp, position );
        movement_vec.z = temp.z;
    }
    if ( dirs_to_check[ FACE_BACK ] ) {
        glm::vec3 temp(0, 0, movement_vec.z);
        check_collides_with_player( world, temp, position );
        movement_vec.z = temp.z;
    }
    free( dirs_to_check );
    pr_debug("Standing:%d", standing);
    *out_standing = standing;
}