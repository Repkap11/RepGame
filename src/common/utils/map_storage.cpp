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

void MapStorage::init( const char *world_name ) {
    char *dir = getRepGamePath( );
    snprintf( this->map_name, MAP_NAME_MAX_LENGTH, "%s%s%s", dir, REPGAME_PATH_DIVIDOR, world_name );
    pr_debug( "Loading map from:%s", this->map_name );
    mkdir_p( this->map_name );
    free( dir );
}

char *MapStorage::get_map_name( ) {
    return this->map_name;
}

typedef struct __attribute__( ( packed ) ) {
    BlockState blockState;
    unsigned int num;
} STORAGE_TYPE;

void MapStorage::persist_chunk( const Chunk &chunk ) {
    if ( chunk.dirty ) {
        this->persist_dirty_blocks( chunk.chunk_pos, chunk.blocks );
    }
}

void MapStorage::persist_dirty_blocks( const glm::ivec3 &chunk_offset, const BlockState *blocks ) {
    if ( blocks == NULL ) {
        return;
    }
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( write_ptr == NULL ) {
        pr_debug( "fopen failed for chunk:%s", file_name );
        return;
    }
    uint32_t storage_type_size = sizeof( STORAGE_TYPE );
    // uint32_t storage_type_size = 8;
    fwrite( &storage_type_size, sizeof( uint32_t ), 1, write_ptr );
    fflush( write_ptr );

    STORAGE_TYPE &persist_data = *( STORAGE_TYPE * )calloc( 1, sizeof( STORAGE_TYPE ) );
    unsigned int num_same_blocks = 0;
    int total_num_blocks = 0;
    BlockState previousBlockState = { BLOCK_STATE_LAST_BLOCK_ID };
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        BlockState blockState = blocks[ i ];
        if ( BlockStates_equal( blockState, previousBlockState ) ) {
            num_same_blocks++;
        } else {
            if ( num_same_blocks > 0 ) {
                persist_data.blockState = previousBlockState;
                persist_data.num = num_same_blocks;
                total_num_blocks += num_same_blocks;
                fwrite( &persist_data, sizeof( STORAGE_TYPE ), 1, write_ptr );
                fflush( write_ptr );
            }
            previousBlockState = blockState;
            num_same_blocks = 1;
        }
    }
    persist_data.blockState = previousBlockState;
    persist_data.num = num_same_blocks;
    total_num_blocks += num_same_blocks;

    // persist_data_index++;

    int expected_num_saved_blocks = CHUNK_BLOCK_SIZE;
    if ( total_num_blocks != expected_num_saved_blocks ) {
        pr_debug( "Didn't get enough blocks save" );
    }
    fwrite( &persist_data, sizeof( STORAGE_TYPE ), 1, write_ptr );
    fflush( write_ptr );
    fclose( write_ptr );
    free( &persist_data );
}

int MapStorage::check_if_chunk_exists( const glm::ivec3 &chunk_offset ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    if ( access( file_name, F_OK ) != -1 ) {
        return 1;
    } else {
        return 0;
    }
}

int MapStorage::load_chunk( Chunk &chunk ) {
    return this->load_blocks( chunk.chunk_pos, chunk.blocks, chunk.dirty, false );
}
int MapStorage::load_blocks( const glm::ivec3 &chunk_offset, BlockState *blocks, int &dirty, bool expect_empty_blocks ) {

    if ( !check_if_chunk_exists( chunk_offset ) ) {
        // This chunk isn't in a file, return 0 so the chunk gets loaded
        return 0;
    }

    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    FILE *read_ptr;
    read_ptr = fopen( file_name, "rb" );
    uint32_t storage_type_size = 0;
    size_t result_size = fread( &storage_type_size, sizeof( uint32_t ), 1, read_ptr );
    if ( result_size != 1 ) {
        fclose( read_ptr );
        return 0;
    }
    // pr_debug( "Got storage size:%d", storage_type_size );

    int block_index = 0;

    bool loading_old_format_chunk = storage_type_size != sizeof( STORAGE_TYPE );
    if ( loading_old_format_chunk ) {
        dirty = true;
        pr_debug( "Loaded old chunk:%s with state size:%d expected:%d", file_name, storage_type_size, ( int )sizeof( STORAGE_TYPE ) );
    }

    int storage_block_state_size = storage_type_size - sizeof( unsigned int );

    char *persist_data = ( char * )calloc( CHUNK_BLOCK_SIZE, storage_type_size );
    unsigned int persist_data_length = fread( persist_data, storage_type_size, CHUNK_BLOCK_SIZE, read_ptr );
    // pr_debug( "Got data length:%d", persist_data_length );
    fclose( read_ptr );

    if ( storage_block_state_size > ( int )sizeof( BlockState ) ) {
        pr_debug( "Smaller" );
        storage_block_state_size = ( int )sizeof( BlockState );
    }

    for ( unsigned int i = 0; i < persist_data_length; i++ ) {
        char *block_storage = &persist_data[ i * storage_type_size ];
        unsigned int block_storage_num = *( unsigned int * )&persist_data[ ( ( i + 1 ) * storage_type_size ) - sizeof( unsigned int ) ];

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
            if ( storage_block_state_size <= 16 ) {
                blockState.display_id = blockState.id;
            }
        }
        if ( !expect_empty_blocks && blockState.id >= LAST_BLOCK_ID ) {
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
    free( persist_data );
    return 1;
}

// Layout written by versions before world position switched float -> double.
// Old save files are still accepted and their fields widened on load.
struct __attribute__( ( packed ) ) PlayerDataLegacy {
    int reserved;
    float world_x;
    float world_y;
    float world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
};

int MapStorage::read_player_data( PlayerData &player_data ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_PLAYER_DATA, this->map_name );
    FILE *read_ptr = fopen( file_name, "rb" );
    if ( !read_ptr ) {
        pr_debug( "No player data:%s (%p)", file_name, read_ptr );
        return 0;
    }
    memset( &player_data, 0, sizeof( PlayerData ) );
    unsigned char buffer[ sizeof( PlayerData ) ];
    size_t persist_data_length = fread( buffer, 1, sizeof( buffer ), read_ptr );
    fclose( read_ptr );
    if ( persist_data_length == sizeof( PlayerData ) ) {
        memcpy( &player_data, buffer, sizeof( PlayerData ) );
        return 1;
    }
    if ( persist_data_length == sizeof( PlayerDataLegacy ) ) {
        PlayerDataLegacy legacy;
        memcpy( &legacy, buffer, sizeof( PlayerDataLegacy ) );
        player_data.world_x = legacy.world_x;
        player_data.world_y = legacy.world_y;
        player_data.world_z = legacy.world_z;
        player_data.angle_H = legacy.angle_H;
        player_data.angle_V = legacy.angle_V;
        player_data.flying = legacy.flying;
        player_data.no_clip = legacy.no_clip;
        player_data.worldDrawQuality = legacy.worldDrawQuality;
        memcpy( player_data.hotbar_inventory, legacy.hotbar_inventory, sizeof( legacy.hotbar_inventory ) );
        player_data.selected_hotbar_slot = legacy.selected_hotbar_slot;
        player_data.game_mode = legacy.game_mode;
        memcpy( player_data.survival_inventory, legacy.survival_inventory, sizeof( legacy.survival_inventory ) );
        return 1;
    }
    pr_debug( "Warning, wrong size player data. Read:%d expected:%d or %d", ( int )persist_data_length, ( int )sizeof( PlayerData ), ( int )sizeof( PlayerDataLegacy ) );
    return 0;
}

void MapStorage::write_player_data( const PlayerData &player_data ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_PLAYER_DATA, this->map_name );
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( !write_ptr ) {
        pr_debug( "Can't write player:%s (%p)", file_name, write_ptr );
        return;
    }
    fwrite( &player_data, sizeof( PlayerData ), 1, write_ptr );
    fclose( write_ptr );
}
