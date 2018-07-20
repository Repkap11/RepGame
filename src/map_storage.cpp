#include "map_storage.h"
#include "RepGame.h"
#include "constants.h"
#include <errno.h>
#include <limits.h> /* PATH_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* mkdir(2) */
#include <unistd.h>
#include "file_utils.h"

#define CHUNK_NAME_LENGTH 200

char file_root[ CHUNK_NAME_LENGTH ] = "%s/chunk_%d_%d_%d";
char map_name[ CHUNK_NAME_LENGTH ];

int mkdir_p( const char *path ) {
    const size_t len = strlen( path );
    char _path[ PATH_MAX ];
    char *p;

    errno = 0;

    /* Copy string so its mutable */
    if ( len > sizeof( _path ) - 1 ) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy( _path, path );

    /* Iterate the string */
    for ( p = _path + 1; *p; p++ ) {
        if ( *p == '/' ) {
            /* Temporarily truncate */
            *p = '\0';

            if ( mkdir( _path, S_IRWXU ) != 0 ) {
                if ( errno != EEXIST )
                    return -1;
            }

            *p = '/';
        }
    }

    if ( mkdir( _path, S_IRWXU ) != 0 ) {
        if ( errno != EEXIST )
            return -1;
    }

    return 0;
}

void map_storage_init( ) {
    char *dir = getRepGamePath( );
    char bufferSky[ BUFSIZ ];
    sprintf( map_name, "%s%s", dir, "/World1" );
    mkdir_p( map_name );
}

void map_storage_cleanup( ) {
}

#define STORAGE_TYPE unsigned char

void map_storage_persist( Chunk *chunk ) {
    if ( chunk->ditry ) {
        int chunk_offset_x = chunk->chunk_x * CHUNK_SIZE;
        int chunk_offset_y = chunk->chunk_y * CHUNK_SIZE;
        int chunk_offset_z = chunk->chunk_z * CHUNK_SIZE;
        char file_name[ CHUNK_NAME_LENGTH ];
        snprintf( file_name, CHUNK_NAME_LENGTH, file_root, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z );

        FILE *write_ptr;
        STORAGE_TYPE persist_data[ CHUNK_BLOCK_SIZE ];
        Block *blocks = chunk->blocks;
        if ( blocks == 0 ) {
            return;
        }
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            Block *block = &blocks[ i ];
            BlockDefinition *blockDef = block->blockDef;
            if ( blockDef == NULL ) {
                pr_debug( "Block null?" );
                continue;
            }
            persist_data[ i ] = (STORAGE_TYPE)(blockDef->id);
        }
        write_ptr = fopen( file_name, "wb" );
        fwrite( persist_data, CHUNK_BLOCK_SIZE * sizeof( STORAGE_TYPE ), 1, write_ptr );
        fclose( write_ptr );
    }
}

int check_if_chunk_exists( TRIP_ARGS( int chunk_offset_ ) ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root, map_name, TRIP_ARGS( chunk_offset_ ) );
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

    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root, map_name, TRIP_ARGS( chunk_offset_ ) );

    FILE *read_ptr;
    STORAGE_TYPE persist_data[ CHUNK_BLOCK_SIZE ];
    Block *blocks = chunk->blocks;
    read_ptr = fopen( file_name, "rb" );
    int rc = fread( persist_data, CHUNK_BLOCK_SIZE * sizeof( STORAGE_TYPE ), 1, read_ptr );
    fclose( read_ptr );

    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        BlockID blockId = ( BlockID )( persist_data[ i ] );
        if (blockId >= LAST_BLOCK_ID){
            pr_debug("Got strange block:%d", blockId);
            blockId = TNT;
        }
        chunk->blocks[ i ].blockDef = block_definition_get_definition( blockId );
    }
    return 1;
}
