#include "map_storage.h"
#include "RepGame.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHUNK_NAME_LENGTH 50

char file_root[ CHUNK_NAME_LENGTH ] = "./chunk_%d_%d_%d";

void map_storage_init( ) {
}

void map_storage_cleanup( ) {
}

void map_storage_persist( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root, chunk_offset_x, chunk_offset_y, chunk_offset_z );

    FILE *write_ptr;
    BlockID persist_data[ CHUNK_BLOCK_SIZE ];
    Block *blocks = chunk->blocks;
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        persist_data[ i ] = blocks[ i ].blockDef->id;
    }
    write_ptr = fopen( file_name, "wb" );
    fwrite( persist_data, CHUNK_BLOCK_SIZE * sizeof( BlockID ), 1, write_ptr );
    fclose( write_ptr );
}

int check_if_chunk_exists( int chunk_offset_x, int chunk_offset_y, int chunk_offset_z ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root, chunk_offset_x, chunk_offset_y, chunk_offset_z );
    if ( access( file_name, F_OK ) != -1 ) {
        return 1;
    } else {
        return 0;
    }
}

int map_storage_load( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
    int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
    int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;

    if ( !check_if_chunk_exists( chunk_offset_x, chunk_offset_y, chunk_offset_z ) ) {
        // This chunk isn't in a file, return 0 so the chunk gets loaded
        return 0;
    }

    if ( chunk->blocks == NULL ) {
        chunk->blocks = calloc( CHUNK_BLOCK_SIZE, sizeof( Block ) );
    } else {
        // This chunks memory is already allocated, its likely reused?
    }
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root, chunk_offset_x, chunk_offset_y, chunk_offset_z );

    FILE *read_ptr;
    BlockID persist_data[ CHUNK_BLOCK_SIZE ];
    Block *blocks = chunk->blocks;
    read_ptr = fopen( file_name, "rb" );
    int rc = fread( persist_data, CHUNK_BLOCK_SIZE * sizeof( BlockID ), 1, read_ptr );
    fclose( read_ptr );

    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        chunk->blocks[ i ].blockDef = block_definition_get_definition( persist_data[ i ] );
    }
    return 1;
}
