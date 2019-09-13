#include <errno.h>
#include <limits.h> /* PATH_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* mkdir(2) */
#include <unistd.h>

#include "common/utils/map_storage.hpp"
#include "common/RepGame.hpp"
#include "common/constants.hpp"
#include "common/utils/file_utils.hpp"

#define CHUNK_NAME_LENGTH 200

const char file_root_chunk[ CHUNK_NAME_LENGTH ] = "%s/chunk_%d_%d_%d";
const char file_root_player_data[ CHUNK_NAME_LENGTH ] = "%s/player.dat";

char map_name[ CHUNK_NAME_LENGTH ];

#if defined( REPGAME_WINDOWS )
#define MKDIR_ETRA_FLAGS
#else
#define MKDIR_ETRA_FLAGS , S_IRWXU
#endif

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
    snprintf( _path, PATH_MAX, "%s", path );

    /* Iterate the string */
    for ( p = _path + 1; *p; p++ ) {
        if ( *p == '/' ) {
            /* Temporarily truncate */
            *p = '\0';

            if ( mkdir( _path MKDIR_ETRA_FLAGS ) != 0 ) {
                if ( errno != EEXIST )
                    return -1;
            }

            *p = '/';
        }
    }

    if ( mkdir( _path MKDIR_ETRA_FLAGS ) != 0 ) {
        if ( errno != EEXIST )
            return -1;
    }

    return 0;
}

void map_storage_init( const char *world_name ) {
    char *dir = getRepGamePath( );
    snprintf( map_name, CHUNK_NAME_LENGTH, "%s%s%s", dir, REPGAME_PATH_DIVIDOR, world_name );
    pr_debug( "Loading map from:%s", map_name );
    mkdir_p( map_name );
    free( dir );
}

void map_storage_cleanup( ) {
}

#define STORAGE_TYPE_BLOCK_ID uint16_t
#define STORAGE_TYPE_NUM_BLOCKS unsigned int

typedef struct {
    STORAGE_TYPE_BLOCK_ID block_id;
    STORAGE_TYPE_NUM_BLOCKS num;
} STORAGE_TYPE;

void map_storage_persist( Chunk *chunk ) {
    if ( chunk->ditry ) {
        int chunk_offset_x = chunk->chunk_x;
        int chunk_offset_y = chunk->chunk_y;
        int chunk_offset_z = chunk->chunk_z;
        char file_name[ CHUNK_NAME_LENGTH ];
        snprintf( file_name, CHUNK_NAME_LENGTH, file_root_chunk, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z );

        FILE *write_ptr;
        STORAGE_TYPE persist_data[ CHUNK_BLOCK_SIZE ];
        BlockState *blocks = chunk->blocks;
        if ( blocks == 0 ) {
            return;
        }
        int persist_data_index = -1;
        STORAGE_TYPE_NUM_BLOCKS num_same_blocks = 0;
        int total_num_blocks = 0;
        STORAGE_TYPE_BLOCK_ID previous_id = LAST_BLOCK_ID;
        BlockState previousBlockState = {LAST_BLOCK_ID, BLOCK_ROTATE_0};
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            BlockState blockState = blocks[ i ];
            if ( blockState.id == previousBlockState.id && blockState.rotation == previousBlockState.rotation ) {
                num_same_blocks++;
            } else {
                if ( num_same_blocks > 0 ) {
                    persist_data[ persist_data_index ].block_id = previous_id;
                    persist_data[ persist_data_index ].num = num_same_blocks;
                    total_num_blocks += num_same_blocks;
                }
                persist_data_index++;
                previous_id = ( ( STORAGE_TYPE_BLOCK_ID )blockState.id ) | ( ( ( STORAGE_TYPE_BLOCK_ID )blockState.rotation ) << 14 );
                num_same_blocks = 1;
            }
        }
        persist_data[ persist_data_index ].block_id = previous_id;
        persist_data[ persist_data_index ].num = num_same_blocks;
        total_num_blocks += num_same_blocks;

        persist_data_index++;

        int expected_num_saved_blocks = CHUNK_BLOCK_SIZE;
        if ( total_num_blocks != expected_num_saved_blocks ) {
            pr_debug( "Didn't get enough blocks save" );
        }
        write_ptr = fopen( file_name, "wb" );
        fwrite( persist_data, sizeof( STORAGE_TYPE ), persist_data_index, write_ptr );
        fclose( write_ptr );
    }
}

int check_if_chunk_exists( TRIP_ARGS( int chunk_offset_ ) ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root_chunk, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z );
    if ( access( file_name, F_OK ) != -1 ) {
        return 1;
    } else {
        return 0;
    }
}

int map_storage_load( Chunk *chunk ) {
    int chunk_offset_x = chunk->chunk_x;
    int chunk_offset_y = chunk->chunk_y;
    int chunk_offset_z = chunk->chunk_z;

    if ( !check_if_chunk_exists( chunk_offset_x, chunk_offset_y, chunk_offset_z ) ) {
        // This chunk isn't in a file, return 0 so the chunk gets loaded
        return 0;
    }

    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root_chunk, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z );

    FILE *read_ptr;
    STORAGE_TYPE persist_data[ CHUNK_BLOCK_SIZE ];
    BlockState *blocks = chunk->blocks;
    read_ptr = fopen( file_name, "rb" );
    int persist_data_length = fread( persist_data, sizeof( STORAGE_TYPE ), CHUNK_BLOCK_SIZE, read_ptr );
    fclose( read_ptr );

    int block_index = 0;
    for ( int i = 0; i < persist_data_length; i++ ) {
        STORAGE_TYPE block_storage = persist_data[ i ];
        STORAGE_TYPE_BLOCK_ID block_packed = block_storage.block_id;
        BlockState blockState;
        blockState.id = ( BlockID )( block_packed & ~( 0b11 << 14 ) );
        blockState.rotation = block_packed >> 14;
        if ( blockState.id >= LAST_BLOCK_ID ) {
            pr_debug( "Got strange block:%d", block_storage.block_id );
            block_storage.block_id = TNT;
        }
        for ( unsigned int j = 0; j < block_storage.num; j++ ) {
            blocks[ block_index++ ] = blockState;
        }
    }
    int expected_num_blocks = CHUNK_BLOCK_SIZE;
    if ( block_index != expected_num_blocks ) {
        pr_debug( "Didn't get enough blocks load" );
    }
    return 1;
}

int map_storage_read_player_data( PlayerData *player_data ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root_player_data, map_name );
    FILE *read_ptr = fopen( file_name, "rb" );
    if ( !read_ptr ) {
        pr_debug( "No player data:%s (%p)", file_name, read_ptr );
        return 0;
    }
    int persist_data_length = fread( player_data, 1, sizeof( PlayerData ), read_ptr );
    if ( persist_data_length != sizeof( PlayerData ) ) {
        pr_debug( "Warning, wrong size player data. Read:%d expected:%d", persist_data_length, ( int )sizeof( PlayerData ) );
    }
    fclose( read_ptr );
    return 1;
}

void map_storage_write_player_data( PlayerData *player_data ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    snprintf( file_name, CHUNK_NAME_LENGTH, file_root_player_data, map_name );
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( !write_ptr ) {
        pr_debug( "Can't write player:%s (%p)", file_name, write_ptr );
        return;
    }
    fwrite( player_data, sizeof( PlayerData ), 1, write_ptr );
    fclose( write_ptr );
}
