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

#define STORAGE_TYPE_SIZE_HEADER uint32_t

#define STORAGE_TYPE_BLOCK_ID BlockState
#define STORAGE_TYPE_NUM_BLOCKS unsigned int

typedef struct {
    STORAGE_TYPE_BLOCK_ID blockState;
    STORAGE_TYPE_NUM_BLOCKS num;
} STORAGE_TYPE;

void map_storage_persist( Chunk *chunk ) {
    if ( chunk->dirty ) {
        int chunk_offset_x = chunk->chunk_x;
        int chunk_offset_y = chunk->chunk_y;
        int chunk_offset_z = chunk->chunk_z;
        char file_name[ CHUNK_NAME_LENGTH ];
        if ( snprintf( file_name, CHUNK_NAME_LENGTH, file_root_chunk, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z ) != CHUNK_NAME_LENGTH ) {
        }

        FILE *write_ptr;
        STORAGE_TYPE persist_data[ CHUNK_BLOCK_SIZE ];
        BlockState *blocks = chunk->blocks;
        if ( blocks == 0 ) {
            return;
        }
        int persist_data_index = -1;
        STORAGE_TYPE_NUM_BLOCKS num_same_blocks = 0;
        int total_num_blocks = 0;
        BlockState previousBlockState = { BLOCK_STATE_LAST_BLOCK_ID };
        for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
            BlockState blockState = blocks[ i ];
            if ( blockState.id == previousBlockState.id ) {
                num_same_blocks++;
            } else {
                if ( num_same_blocks > 0 ) {
                    persist_data[ persist_data_index ].blockState = previousBlockState;
                    persist_data[ persist_data_index ].num = num_same_blocks;
                    total_num_blocks += num_same_blocks;
                }
                persist_data_index++;
                previousBlockState = blockState;
                num_same_blocks = 1;
            }
        }
        persist_data[ persist_data_index ].blockState = previousBlockState;
        persist_data[ persist_data_index ].num = num_same_blocks;
        total_num_blocks += num_same_blocks;

        persist_data_index++;

        int expected_num_saved_blocks = CHUNK_BLOCK_SIZE;
        if ( total_num_blocks != expected_num_saved_blocks ) {
            pr_debug( "Didn't get enough blocks save" );
        }
        write_ptr = fopen( file_name, "wb" );
        uint32_t storage_type_size = sizeof( STORAGE_TYPE );
        // uint32_t storage_type_size = 8;
        fwrite( &storage_type_size, sizeof( uint32_t ), 1, write_ptr );
        fwrite( persist_data, sizeof( STORAGE_TYPE ), persist_data_index, write_ptr );
        fclose( write_ptr );
    }
}

int check_if_chunk_exists( TRIP_ARGS( int chunk_offset_ ) ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    if ( snprintf( file_name, CHUNK_NAME_LENGTH, file_root_chunk, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z ) != CHUNK_NAME_LENGTH ) {
    }
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
    if ( snprintf( file_name, CHUNK_NAME_LENGTH, file_root_chunk, map_name, chunk_offset_x, chunk_offset_y, chunk_offset_z ) != CHUNK_NAME_LENGTH ) {
    }
    FILE *read_ptr;
    BlockState *blocks = chunk->blocks;
    read_ptr = fopen( file_name, "rb" );
    uint32_t storage_type_size = 0;
    fread( &storage_type_size, sizeof( uint32_t ), 1, read_ptr );
    // pr_debug( "Got storage size:%d", storage_type_size );

    char *persist_data = ( char * )calloc( CHUNK_BLOCK_SIZE, storage_type_size );
    unsigned int persist_data_length = fread( persist_data, storage_type_size, CHUNK_BLOCK_SIZE, read_ptr );
    // pr_debug( "Got data length:%d", persist_data_length );
    fclose( read_ptr );

    int block_index = 0;
    int storage_block_state_size = storage_type_size - sizeof( unsigned int );

    bool loading_old_format_chunk = storage_block_state_size != sizeof( BlockState );
    if ( loading_old_format_chunk ) {
        chunk->dirty = true;
        pr_debug( "Loaded old chunk with state size:%d expected:%d", storage_block_state_size, (int)sizeof( BlockState ) );
    }
    for ( unsigned int i = 0; i < persist_data_length; i++ ) {
        char *block_storage = &persist_data[ i * storage_type_size ];
        unsigned int block_storage_num = *( unsigned int * )&persist_data[ ( ( i + 1 ) * storage_type_size ) - sizeof( STORAGE_TYPE_NUM_BLOCKS ) ];

        BlockState blockState = BLOCK_STATE_AIR;
        memset( &blockState, 0, sizeof( BlockState ) );
        // pr_debug( "Copy size:%d", storage_type_size - sizeof( unsigned int )-2 );
        int storage_offset_size = 0;
        if ( storage_type_size == 8 ) {
            storage_offset_size = 2;
        }
        memcpy( &blockState, block_storage, storage_block_state_size - storage_offset_size );
        if ( loading_old_format_chunk ) {
            // Per block state mitigations
            if ( storage_block_state_size <= 12 ) {
                blockState.display_id = blockState.id;
            }
        }
        if ( blockState.id >= LAST_BLOCK_ID ) {
            pr_debug( "Got strange block in %s :%d", file_name, blockState.id );
            // blockState.id = GOLD_BLOCK;
        }
        for ( unsigned int j = 0; j < block_storage_num; j++ ) {
            blocks[ block_index++ ] = blockState;
        }
    }
    int expected_num_blocks = CHUNK_BLOCK_SIZE;
    if ( block_index != expected_num_blocks ) {
        pr_debug( "Didn't get enough blocks load:%s", file_name );
    }
    return 1;
}

int map_storage_read_player_data( PlayerData *player_data ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    if ( snprintf( file_name, CHUNK_NAME_LENGTH, file_root_player_data, map_name ) != CHUNK_NAME_LENGTH ) {
    }
    FILE *read_ptr = fopen( file_name, "rb" );
    if ( !read_ptr ) {
        pr_debug( "No player data:%s (%p)", file_name, read_ptr );
        return 0;
    }
    memset( player_data, 0, sizeof( PlayerData ) );
    int persist_data_length = fread( player_data, 1, sizeof( PlayerData ), read_ptr );
    if ( persist_data_length != sizeof( PlayerData ) ) {
        pr_debug( "Warning, wrong size player data. Read:%d expected:%d", persist_data_length, ( int )sizeof( PlayerData ) );
    }
    fclose( read_ptr );
    return 1;
}

void map_storage_write_player_data( PlayerData *player_data ) {
    char file_name[ CHUNK_NAME_LENGTH ];
    if ( snprintf( file_name, CHUNK_NAME_LENGTH, file_root_player_data, map_name ) != CHUNK_NAME_LENGTH ) {
    }
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( !write_ptr ) {
        pr_debug( "Can't write player:%s (%p)", file_name, write_ptr );
        return;
    }
    fwrite( player_data, sizeof( PlayerData ), 1, write_ptr );
    fclose( write_ptr );
}
