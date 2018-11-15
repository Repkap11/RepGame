// // Returns the worls coord from the screen coords xy
// void getPosFromMouse( int x, int y, TRIP_ARGS( double *out_ ) ) {
//     GLint viewport[ 4 ];
//     GLdouble modelview[ 16 ];
//     GLdouble projection[ 16 ];
//     GLfloat winX, winY, winZ;
//     GLdouble posX, posY, posZ;
//     glGetDoublev( GL_MODELVIEW_MATRIX, modelview );
//     glGetDoublev( GL_PROJECTION_MATRIX, projection );
//     glGetIntegerv( GL_VIEWPORT, viewport );
//     winX = ( float )x;
//     winY = ( float )viewport[ 3 ] - ( float )y;
//     glReadPixels( x, ( int )winY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &winZ );

//     gluUnProject( winX, winY, winZ, modelview, projection, viewport, out_x, out_y, out_z );
// }

// static void calculateMousePos( int arg, TRIP_ARGS( int *out_create_ ), TRIP_ARGS( int *out_destroy_ ) ) {
//     double world_mouse_x, world_mouse_y, world_mouse_z = 0;
//     getPosFromMouse( globalGameState.screen.width / 2, globalGameState.screen.height / 2, &world_mouse_x, &world_mouse_y, &world_mouse_z );
//     int camera_x_round = round( globalGameState.camera.x );
//     int camera_y_round = round( globalGameState.camera.y );
//     int camera_z_round = round( globalGameState.camera.z );
//     // pr_debug( "Mouse x:%f y:%f z:%f", world_mouse_x, world_mouse_y, world_mouse_z );
//     int world_x_round = round( world_mouse_x );
//     int world_y_round = round( world_mouse_y );
//     int world_z_round = round( world_mouse_z );

//     double dis_x = world_x_round - world_mouse_x;
//     double dis_y = world_y_round - world_mouse_y;
//     double dis_z = world_z_round - world_mouse_z;

//     double dis_x_abs = fabs( dis_x );
//     double dis_y_abs = fabs( dis_y );
//     double dis_z_abs = fabs( dis_z );

//     int face_x = ( dis_x_abs < 0.01 );
//     int face_y = ( dis_y_abs < 0.01 );
//     int face_z = ( dis_z_abs < 0.01 );
//     // pr_debug( "Face x:%d y:%d z:%d", face_x, face_y, face_z );

//     double sig_x = ( round( world_mouse_x ) - floor( globalGameState.camera.x ) == 0 ) ? -1 : ( ( round( world_mouse_x ) - floor( globalGameState.camera.x ) ) / fabs( floor( globalGameState.camera.x ) - round( world_mouse_x ) ) );
//     double sig_y = ( round( world_mouse_y ) - floor( globalGameState.camera.y ) == 0 ) ? -1 : ( ( round( world_mouse_y ) - floor( globalGameState.camera.y ) ) / fabs( floor( globalGameState.camera.y ) - round( world_mouse_y ) ) );
//     double sig_z = ( round( world_mouse_z ) - floor( globalGameState.camera.z ) == 0 ) ? -1 : ( ( round( world_mouse_z ) - floor( globalGameState.camera.z ) ) / fabs( floor( globalGameState.camera.z ) - round( world_mouse_z ) ) );
//     // pr_debug( "Sig x:%+2f y:%+2f z:%+2f", sig_x, sig_y, sig_z );
//     // pr_debug( "Faceing x:%+2.0f y:%+2.0f z:%+2.0f", sig_x * face_x, sig_y * face_y, sig_z * face_z );

//     int offset_x = sig_x * face_x;
//     int offset_y = sig_y * face_y;
//     int offset_z = sig_z * face_z;

//     // int offset_x = face_x * ( world_x_round <= camera_x_round );
//     // int offset_y = face_y * ( world_y_round <= camera_y_round );
//     // int offset_z = face_z * ( world_z_round <= camera_z_round );
//     // pr_debug( " Offset x:%d y:%d z:%d", offset_x, offset_y, offset_z );

//     // int offset_x2 = ( world_x_round == camera_x_round ) && face_x;
//     // int offset_y2 = ( world_y_round == camera_y_round ) && face_y;
//     // int offset_z2 = ( world_z_round == camera_z_round ) && face_z;
//     // pr_debug( "Offse2 x:%d y:%d z:%d", offset_x2, offset_y2, offset_z2 );

//     int world_x = face_x ? world_x_round : floor( world_mouse_x );
//     int world_y = face_y ? world_y_round : floor( world_mouse_y );
//     int world_z = face_z ? world_z_round : floor( world_mouse_z );
//     // pr_debug( "World %d x:%d y:%d z:%d", arg, world_x, world_y, world_z );

//     world_x += ( offset_x == 1 ? 0 : offset_x );
//     world_y += ( offset_y == 1 ? 0 : offset_y );
//     world_z += ( offset_z == 1 ? 0 : offset_z );

//     *out_destroy_x = world_x;
//     *out_destroy_y = world_y;
//     *out_destroy_z = world_z;

//     *out_create_x = world_x - offset_x;
//     *out_create_y = world_y - offset_y;
//     *out_create_z = world_z - offset_z;
//     // The player has selected this block (unless there are edge problems)
// }

// if ( globalGameState.physics.chunk ) {

//     // The coordinates of the player within the chunk (0 to 7.9999)
//     int chunk_offset_x = floor( globalGameState.camera.x - globalGameState.physics.chunk->chunk_x * CHUNK_SIZE );
//     int chunk_offset_y = floor( globalGameState.camera.y - EYE_POSITION_OFFSET - globalGameState.physics.chunk->chunk_y * CHUNK_SIZE );
//     int chunk_offset_z = floor( globalGameState.camera.z - globalGameState.physics.chunk->chunk_z * CHUNK_SIZE );

//     // The coordinates of the player within the block (0 to 1)
//     float block_offset_x = globalGameState.camera.x - floor( globalGameState.camera.x );
//     float block_offset_y = globalGameState.camera.y - EYE_POSITION_OFFSET - floor( globalGameState.camera.y - EYE_POSITION_OFFSET );
//     float block_offset_z = globalGameState.camera.z - floor( globalGameState.camera.z );
//     // pr_debug( "block_offset: %f %f %f", TRIP_ARGS( block_offset_ ) );

//     for ( int i = -1; i < 2; i++ ) {
//         for ( int j = -1; j < 2; j++ ) {
//             for ( int k = -1; k < 2; k++ ) {
//                 if ( ( ( i != 0 ) + ( j != 0 ) + ( k != 0 ) ) > 1 ) {
//                     // continue;
//                 }
//                 float adjusted_half_width = character_half_width;
//                 float adjusted_half_height = character_half_height;
//                 // Using the direction ijk, calculate the players extent in that direction.
//                 if ( i != 0 && k != 0 ) {
//                     // We're on a diagional, which means the
//                     adjusted_half_width = character_half_width / sqrt( 2 );
//                 } else {
//                     // We're only in 1 direction, so just use the normal size
//                 }
//                 if ( ( i != 0 || k != 0 ) && j == -1 ) {
//                     adjusted_half_height -= 0.1f;
//                 }
//                 // pr_debug( "" );
//                 // The players extent in that direction
//                 float diff_x = ( block_offset_x + ( float )i * adjusted_half_width );
//                 float diff_y = ( block_offset_y + ( float )j * adjusted_half_height );
//                 float diff_z = ( block_offset_z + ( float )k * adjusted_half_width );
//                 if ( i == 0 && j == -1 && k == 0 ) {
//                     // pr_debug( "diff below: %f %f %f", TRIP_ARGS( diff_ ) );
//                 }
//                 // If the player extends into this block, we need to check it
//                 int collide_px = ( diff_x > 1.0f );
//                 int collide_nx = ( diff_x < 0.0f );
//                 int collide_py = ( diff_y > 1.0f );
//                 int collide_ny = ( diff_y < 0.0f );
//                 int collide_pz = ( diff_z > 1.0f );
//                 int collide_nz = ( diff_z < 0.0f );

//                 if ( ( collide_px || collide_nx || i == 0 ) && //
//                      ( collide_py || collide_ny || j == 0 ) && //
//                      ( collide_pz || collide_nz || k == 0 ) ) {
//                     // We need to check this block because it is close enough to us

//                     int block_position_x = floor( chunk_offset_x + i );
//                     int block_position_y = floor( chunk_offset_y + j );
//                     int block_position_z = floor( chunk_offset_z + k );
//                     Block *block = chunk_get_block(    //
//                         globalGameState.physics.chunk, //
//                         block_position_x,              //
//                         block_position_y,              //
//                         block_position_z               //
//                         );
//                     if ( check_block( block ) ) {
//                         // pr_debug( "Clip pos %2d %2d %2d : %2d %2d %2d", i, j, k, TRIP_ARGS( block_position_ ) );
//                         // pr_debug( "Collide p:%d %d %d n:%d %d %d dir:%d %d %d", TRIP_ARGS( collide_p ), //
//                         //           TRIP_ARGS( collide_n ),                                               //
//                         //           i, j, k );

//                         if ( !NO_CLIP ) {

//                             if ( collide_px && movement_vector_x > 0.0f ) {
//                                 // movement_vector_x = 0;
//                                 limit_movement_x = diff_x - 1.0f;
//                             }
//                             if ( collide_nx && movement_vector_x < 0.0f ) {
//                                 // movement_vector_x = 0;
//                                 limit_movement_x = diff_x;
//                             }
//                             if ( collide_py && movement_vector_y > 0.0f ) {
//                                 // movement_vector_y = 0;
//                                 limit_movement_y = diff_y - 1.0f;
//                             }
//                             if ( collide_ny && movement_vector_y < 0.0f ) {
//                                 // movement_vector_y = 0;
//                                 limit_movement_y = diff_y;
//                             }
//                             if ( collide_pz && movement_vector_z > 0.0f ) {
//                                 // movement_vector_z = 0;
//                                 limit_movement_z = diff_z - 1.0f;
//                             }
//                             if ( collide_nz && movement_vector_z < 0.0f ) {
//                                 // movement_vector_z = 0;
//                                 limit_movement_z = diff_z;
//                             }
//                         }

//                     } else {
//                         // pr_debug( "No Block %2d %2d %2d : %2d %2d %2d", i, j, k, TRIP_ARGS( block_position_ ) );
//                         // pr_debug( "No Block" );
//                     }
//                 } else {
//                     // pr_debug( "Too Far  %2d %2d %2d : %4.2f %4.2f %4.2f", i, j, k, TRIP_ARGS( diff_ ) );
//                 }
//             }
//         }
//     }

// float move_mag = sqrt( ( movement_vector_x * movement_vector_x ) + ( movement_vector_y * movement_vector_y ) + ( movement_vector_z * movement_vector_z ) );
// pr_debug( "                                                    Movement: Mag:%f %f %f %f", move_mag, TRIP_ARGS( movement_vector_ ) );

// float fraction = 0.020f * MOVEMENT_SENSITIVITY;
// pr_debug("fraction %f", fraction);
// movement_vector_x *= fraction;
// movement_vector_y *= fraction;
// movement_vector_z *= fraction;

// pr_debug( "limit:%f %f %f", TRIP_ARGS( limit_movement_ ) );
// movement_vector_x -= limit_movement_x;
// movement_vector_y -= limit_movement_y;
// movement_vector_z -= limit_movement_z;

// int chunk_loader_is_chunk_loaded( LoadedChunks *loadedChunks, int chunk_x, int chunk_y, int chunk_z ) {
//     for ( int i = 0; i < MAX_LOADED_CHUNKS; i++ ) {
//         Chunk *loadedChunk = &loadedChunks->chunkArray[ i ];
//         if ( loadedChunk->loaded ) {
//             if ( loadedChunk->chunk_x == chunk_x && loadedChunk->chunk_y == chunk_y && loadedChunk->chunk_z == chunk_z ) {
//                 return 1;
//             }
//         }
//     }
//     return 0;
// }

// void chunk_loader_load_chunk( LoadedChunks *loadedChunks, int index, int chunk_x, int chunk_y, int chunk_z ) {
//     Chunk *loadedChunk = &loadedChunks->chunkArray[ index ];
//     // if ( loadedChunk->loaded ) {
// }