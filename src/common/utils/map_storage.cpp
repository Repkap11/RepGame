#include <errno.h>
#include <limits.h> /* PATH_MAX */
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> /* mkdir(2) */
#include <unistd.h>

#include "common/utils/map_storage.hpp"
#include "common/RepGame.hpp"
#include "common/constants.hpp"
#include "common/utils/file_utils.hpp"

// WasmFS opens O_RDONLY OPFS files via a Blob and reads it with
// slice().arrayBuffer(), which in Firefox needs the main thread's event
// loop. When the reader IS the main thread (blocked waiting on the proxy
// worker) that deadlocks. O_RDWR uses FileSystemSyncAccessHandle instead,
// which never touches the main thread.
#if defined( REPGAME_WASM )
#define MAP_READ_MODE "r+b"
#else
#define MAP_READ_MODE "rb"
#endif

void MapStorage::init( const char *world_name ) {
    char *dir = getRepGamePath( );
    snprintf( this->map_name, MAP_NAME_MAX_LENGTH, "%s%s%s", dir, REPGAME_PATH_DIVIDOR, world_name );
    pr_debug( "Loading map from:%s", this->map_name );
    mkdir_p( this->map_name );
    pr_debug( "Map dir ready:%s", this->map_name );
    free( dir );
}

char *MapStorage::get_map_name( ) {
    return this->map_name;
}

typedef struct __attribute__( ( packed ) ) {
    BlockState blockState;
    unsigned int num;
} STORAGE_TYPE;

namespace {
// RLE-encode `get_state(i)` for i in [0, CHUNK_BLOCK_SIZE) into the chunk
// file. Shared by persist_dirty_blocks (server's raw BlockState buffer) and
// persist_chunk (reassembled from the chunk's split cells/extras).
template <typename Getter>
void persist_blocks_encode( MapStorage &storage, const glm::ivec3 &chunk_offset, Getter &&get_state ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, storage.get_map_name( ), chunk_offset.x, chunk_offset.y, chunk_offset.z );
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
        BlockState blockState = get_state( i );
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
#if defined( REPGAME_WASM )
    // File lives on the OPFS backend; fdatasync forces the
    // FileSystemSyncAccessHandle flush so the write survives a tab crash.
    fdatasync( fileno( write_ptr ) );
#endif
    fclose( write_ptr );
    free( &persist_data );
}
} // namespace

void MapStorage::persist_chunk( const Chunk &chunk ) {
    if ( chunk.dirty ) {
        if ( chunk.cells != nullptr ) {
            persist_blocks_encode( *this, chunk.chunk_pos,
                                   [&chunk]( int index ) { return chunk.state_at( index ); } );
        }
        if ( chunk.chests_loaded ) {
            // Chest sidecar file is keyed by world block position.
            ChestContentsMap chest_world_map;
            const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
            for ( const auto &pair : chunk.chests ) {
                const int index = pair.first;
                const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                               ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                               ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
                chest_world_map[ chunk_origin + internal_pos ] = pair.second;
            }
            this->persist_chests( chunk.chunk_pos, chest_world_map );
        }
    }
}

void MapStorage::persist_dirty_blocks( const glm::ivec3 &chunk_offset, const BlockState *blocks ) {
    if ( blocks == NULL ) {
        return;
    }
    persist_blocks_encode( *this, chunk_offset, [blocks]( int index ) { return blocks[ index ]; } );
}

int MapStorage::check_if_chunk_exists( const glm::ivec3 &chunk_offset ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    struct stat st;
    // stat (not access(2)): WasmFS doesn't implement faccessat.
    return stat( file_name, &st ) == 0;
}

// Chest file layout: u32 record_size (forward-compat, like the chunk
// STORAGE_TYPE trick), u32 num_chests, then per chest:
//   { i32 x, i32 y, i32 z (world block pos), InventorySlot slots[CHEST_SLOTS] }.
struct __attribute__( ( packed ) ) ChestFileRecord {
    int x;
    int y;
    int z;
    InventorySlot slots[ CHEST_SLOTS ];
};

void MapStorage::persist_chests( const glm::ivec3 &chunk_offset, const ChestContentsMap &chests ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHEST, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    if ( chests.empty( ) ) {
        // No chests left in this chunk — remove the file so deleted chests
        // don't resurrect on the next load.
        remove( file_name );
        return;
    }
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( write_ptr == NULL ) {
        pr_debug( "fopen failed for chest file:%s", file_name );
        return;
    }
    uint32_t record_size = sizeof( ChestFileRecord );
    fwrite( &record_size, sizeof( uint32_t ), 1, write_ptr );
    uint32_t num_chests = static_cast<uint32_t>( chests.size( ) );
    fwrite( &num_chests, sizeof( uint32_t ), 1, write_ptr );
    for ( const auto &pair : chests ) {
        ChestFileRecord record;
        record.x = pair.first.x;
        record.y = pair.first.y;
        record.z = pair.first.z;
        memcpy( record.slots, pair.second.slots, sizeof( record.slots ) );
        fwrite( &record, sizeof( ChestFileRecord ), 1, write_ptr );
    }
#if defined( REPGAME_WASM )
    fdatasync( fileno( write_ptr ) );
#endif
    fclose( write_ptr );
}

int MapStorage::load_chests( const glm::ivec3 &chunk_offset, ChestContentsMap &out ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHEST, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    FILE *read_ptr = fopen( file_name, MAP_READ_MODE );
    if ( read_ptr == NULL ) {
        return 0;
    }
    uint32_t record_size = 0;
    if ( fread( &record_size, sizeof( uint32_t ), 1, read_ptr ) != 1 ) {
        fclose( read_ptr );
        return 0;
    }
    uint32_t num_chests = 0;
    if ( fread( &num_chests, sizeof( uint32_t ), 1, read_ptr ) != 1 || num_chests > CHUNK_BLOCK_SIZE ) {
        fclose( read_ptr );
        return 0;
    }
    // Read whatever the file's record holds; older/newer builds may have a
    // different record size, so copy only the shared prefix per record.
    const int slot_prefix = offsetof( ChestFileRecord, slots );
    char *raw = static_cast<char *>( calloc( num_chests, record_size ) );
    size_t num_read = fread( raw, record_size, num_chests, read_ptr );
    fclose( read_ptr );
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = &raw[ i * record_size ];
        ChestContents contents; // default: all empty
        glm::ivec3 pos( 0 );
        memcpy( &pos, record, sizeof( glm::ivec3 ) <= (size_t)record_size ? sizeof( glm::ivec3 ) : (size_t)record_size );
        if ( record_size > (uint32_t)slot_prefix ) {
            int slot_bytes = record_size - slot_prefix;
            if ( slot_bytes > (int)sizeof( contents.slots ) ) {
                slot_bytes = (int)sizeof( contents.slots );
            }
            memcpy( contents.slots, record + slot_prefix, slot_bytes );
        }
        for ( int s = 0; s < CHEST_SLOTS; s++ ) {
            if ( contents.slots[ s ].block_id >= LAST_BLOCK_ID ) {
                contents.slots[ s ].block_id = LAST_BLOCK_ID;
                contents.slots[ s ].quantity = 0;
            }
        }
        out[ pos ] = contents;
    }
    free( raw );
    return 1;
}

namespace {
// Decode a saved RLE chunk stream, delivering each cell's BlockState to
// `sink(index, state)`. Shared by load_chunk (splits states into the
// chunk's cells/extras via store_state) and load_blocks (server's raw
// BlockState buffer).
template <typename Sink>
int load_blocks_decode( MapStorage &storage, const glm::ivec3 &chunk_offset, Sink &&sink, int &dirty, bool expect_empty_blocks ) {

    if ( !storage.check_if_chunk_exists( chunk_offset ) ) {
        // This chunk isn't in a file, return 0 so the chunk gets loaded
        return 0;
    }

    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, storage.get_map_name( ), chunk_offset.x, chunk_offset.y, chunk_offset.z );
    FILE *read_ptr;
    read_ptr = fopen( file_name, MAP_READ_MODE );
    if ( read_ptr == NULL ) {
        pr_debug( "fopen failed for chunk:%s", file_name );
        return 0;
    }
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
            sink( block_index++, blockState );
        }
    }
    int expected_num_blocks = CHUNK_BLOCK_SIZE;
    if ( block_index != expected_num_blocks ) {
        pr_debug( "Didn't get enough blocks load:%s", file_name );
    }
    free( persist_data );
    return 1;
}
} // namespace

int MapStorage::load_chunk( Chunk &chunk ) {
    if ( chunk.cells == nullptr ) {
        // cells stays null for chunks with no save file (all-air is implied);
        // a saved chunk needs the array before the RLE stream can decode.
        if ( !this->check_if_chunk_exists( chunk.chunk_pos ) ) {
            return 0;
        }
        chunk.cells = chunk_alloc_cells( );
    }
    return load_blocks_decode( *this, chunk.chunk_pos,
                               [&chunk]( int index, const BlockState &s ) { chunk.store_state( index, s ); },
                               chunk.dirty, false );
}
int MapStorage::load_blocks( const glm::ivec3 &chunk_offset, BlockState *blocks, int &dirty, bool expect_empty_blocks ) {
    return load_blocks_decode( *this, chunk_offset,
                               [blocks]( int index, const BlockState &s ) { blocks[ index ] = s; },
                               dirty, expect_empty_blocks );
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

// Layout written before world_time (day/night clock) was appended.
struct __attribute__( ( packed ) ) PlayerDataNoTime {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
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

// Layout written before render_distance was appended (still has
// world_time). It is a strict prefix of PlayerData, so a plain prefix
// memcpy suffices — render_distance stays 0 (absent) from the memset.
struct __attribute__( ( packed ) ) PlayerDataNoDistance {
    int reserved;
    double world_x;
    double world_y;
    double world_z;
    float angle_H;
    float angle_V;
    bool flying;
    bool no_clip;
    int worldDrawQuality;
    InventorySlot hotbar_inventory[ HOTBAR_WIDTH * HOTBAR_HEIGHT ];
    int selected_hotbar_slot;
    GameMode game_mode;
    InventorySlot survival_inventory[ SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT ];
    long world_time;
};

int MapStorage::read_player_data( PlayerData &player_data ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_PLAYER_DATA, this->map_name );
    FILE *read_ptr = fopen( file_name, MAP_READ_MODE );
    if ( !read_ptr ) {
        pr_debug( "No player data:%s (%p)", file_name, read_ptr );
        return 0;
    }
    memset( &player_data, 0, sizeof( PlayerData ) );
    unsigned char buffer[ sizeof( PlayerData ) ];
    size_t persist_data_length = fread( buffer, 1, sizeof( buffer ), read_ptr );
    fclose( read_ptr );
    pr_debug( "Read player data:%s (%d bytes)", file_name, ( int )persist_data_length );
    if ( persist_data_length == sizeof( PlayerData ) ) {
        memcpy( &player_data, buffer, sizeof( PlayerData ) );
        return 1;
    }
    if ( persist_data_length == sizeof( PlayerDataNoDistance ) ) {
        // Saves from before render_distance existed: strict prefix of
        // PlayerData; render_distance stays 0 (absent) from the memset.
        memcpy( &player_data, buffer, sizeof( PlayerDataNoDistance ) );
        return 1;
    }
    if ( persist_data_length == sizeof( PlayerDataNoTime ) ) {
        // Saves from before world_time existed: copy everything but the
        // appended clock; world_time stays 0 (dawn) from the memset above.
        PlayerDataNoTime legacy;
        memcpy( &legacy, buffer, sizeof( PlayerDataNoTime ) );
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
    pr_debug( "Warning, wrong size player data. Read:%d expected:%d %d %d or %d", ( int )persist_data_length, ( int )sizeof( PlayerData ), ( int )sizeof( PlayerDataNoDistance ), ( int )sizeof( PlayerDataNoTime ), ( int )sizeof( PlayerDataLegacy ) );
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
#if defined( REPGAME_WASM )
    // See persist_dirty_blocks: force the OPFS sync-access-handle flush.
    fdatasync( fileno( write_ptr ) );
#endif
    fclose( write_ptr );
}

// Server-side world clock. A bare int64: the day/night tick count
// (DAY_LENGTH_TICKS per day, see light.hpp). Read returns 0 when the file is
// absent so the caller can apply its own default.
int MapStorage::read_world_time( int64_t &world_time ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_WORLD_DATA, this->map_name );
    FILE *read_ptr = fopen( file_name, MAP_READ_MODE );
    if ( !read_ptr ) {
        return 0;
    }
    const size_t len = fread( &world_time, 1, sizeof( int64_t ), read_ptr );
    fclose( read_ptr );
    if ( len != sizeof( int64_t ) ) {
        pr_debug( "Warning, wrong size world data:%s (%d bytes)", file_name, ( int )len );
        return 0;
    }
    pr_debug( "Read world time:%s (%lld)", file_name, ( long long )world_time );
    return 1;
}

void MapStorage::write_world_time( int64_t world_time ) {
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_WORLD_DATA, this->map_name );
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( !write_ptr ) {
        pr_debug( "Can't write world:%s (%p)", file_name, write_ptr );
        return;
    }
    fwrite( &world_time, sizeof( int64_t ), 1, write_ptr );
    fclose( write_ptr );
}
