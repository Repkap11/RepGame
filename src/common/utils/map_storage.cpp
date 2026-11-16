#include <dirent.h> /* opendir/readdir — save-file index */
#include <limits.h> /* PATH_MAX */
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string> /* std::string — migrate file list */
#include <sys/stat.h> /* mkdir(2) */
#include <unistd.h>

#if defined( REPGAME_MAP_SQLITE )
#include "sqlite3.h"
#endif

#include "common/utils/map_storage.hpp"
#include "common/Platforms.hpp" // IWYU pragma: keep — REPGAME_PATH_DIVIDOR + platform macros
#include "common/Logging.hpp"
#include "common/chunk.hpp"
#include "common/mob_types.hpp" // MOB_TYPE_COUNT
#include "common/vehicle_types.hpp" // vehicle_type_valid
#include "common/player_data.hpp"
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

// Per-chunk world storage is off on platforms that always run connected
// (WASM + Android never persist world state — player.dat is the only
// per-world file they write) and in the block world (no disk I/O at all).
// The blob codecs, save-file index and migrations compile only where
// world state can persist — WASM/Android get inert stubs instead (the
// bottom of this file), so none of the machinery ships in those builds.
#if !defined( REPGAME_WASM ) && !defined( REPGAME_ANDROID )
#define REPGAME_MAP_STORAGE 1
#endif

#if defined( REPGAME_MAP_STORAGE )
static inline bool world_storage_off( ) {
    return repgame_block_world( );
}
// FILE_ROOT_* format and name prefix per MapFileKind (enum order).
static const char *const KIND_FILE_ROOT[ MAP_FILE_KIND_COUNT ] = {
    FILE_ROOT_CHUNK, FILE_ROOT_MOBS, FILE_ROOT_ITEMS, FILE_ROOT_CARTS,
    FILE_ROOT_CHEST, FILE_ROOT_FURNACE, FILE_ROOT_SIGN };
static const char *const KIND_PREFIX[ MAP_FILE_KIND_COUNT ] = {
    "chunk_", "mobs_", "items_", "carts_", "chest_", "furnace_", "sign_" };

namespace {
// In-memory stream the codecs encode into; store_kind_blob commits it.
struct BlobWriter {
    std::vector<unsigned char> data;
    void put( const void *src, size_t n ) {
        const unsigned char *p = static_cast<const unsigned char *>( src );
        data.insert( data.end(), p, p + n );
    }
    template<class T> void put( const T &v ) {
        put( &v, sizeof( T ) );
    }
};

// Cursor over a stored blob. get() is all-or-nothing — a short read just
// fails, and callers treat that as decode failure. records() answers how
// many complete fixed-size records are left for the bulk-read loaders.
struct BlobReader {
    const unsigned char *data = nullptr;
    size_t len = 0;
    size_t off = 0;
    size_t left( ) const {
        return len - off;
    }
    bool get( void *dst, size_t n ) {
        if ( n > left( ) ) {
            return false;
        }
        memcpy( dst, data + off, n );
        off += n;
        return true;
    }
    template<class T> bool get( T &v ) {
        return get( &v, sizeof( T ) );
    }
    const unsigned char *at( ) const {
        return data + off;
    }
    void skip( size_t n ) {
        off += n;
    }
    size_t records( size_t size ) const {
        return size > 0 ? left( ) / size : 0;
    }
};

// Whole file into `out` — the file backend's half of read_kind_blob, and
// the loose-file importer's slurp. 1 when the file existed.
int slurp_file( const char *path, std::vector<unsigned char> &out ) {
    FILE *f = fopen( path, MAP_READ_MODE );
    if ( f == nullptr ) {
        return 0;
    }
    unsigned char buf[ 8192 ];
    for ( ;; ) {
        const size_t n = fread( buf, 1, sizeof( buf ), f );
        out.insert( out.end(), buf, buf + n );
        if ( n < sizeof( buf ) ) {
            break;
        }
    }
    fclose( f );
    return 1;
}
} // namespace

// "<prefix>_<x>_<y>_<z>" -> kind + coords; trailing junk rejects the match.
static bool parse_kind_filename( const char *name, int &kind, glm::ivec3 &pos ) {
    for ( int k = 0; k < MAP_FILE_KIND_COUNT; k++ ) {
        const size_t prefix_len = strlen( KIND_PREFIX[ k ] );
        if ( strncmp( name, KIND_PREFIX[ k ], prefix_len ) != 0 ) {
            continue;
        }
        int x = 0, y = 0, z = 0;
        char tail = 0;
        if ( sscanf( name + prefix_len, "%d_%d_%d%c", &x, &y, &z, &tail ) == 3 ) {
            kind = k;
            pos = glm::ivec3( x, y, z );
            return true;
        }
        return false;
    }
    return false;
}
#endif // REPGAME_MAP_STORAGE

bool MapStorage::detect_flat_world( const char *world_name ) {
    g_repgame_flat_world = false;
    if ( repgame_block_world( ) ) {
        return false;
    }
    char *dir = getRepGamePath( );
    char path[ MAP_NAME_MAX_LENGTH ];
    snprintf( path, MAP_NAME_MAX_LENGTH, "%s%s%s%sworld_type", dir, REPGAME_PATH_DIVIDOR, world_name, REPGAME_PATH_DIVIDOR );
    free( dir );
    FILE *read_ptr = fopen( path, "rb" );
    if ( read_ptr == NULL ) {
        return false;
    }
    char token[ 32 ] = { 0 };
    const int matched = fscanf( read_ptr, "%31s", token );
    fclose( read_ptr );
    g_repgame_flat_world = matched == 1 && strcmp( token, "flat" ) == 0;
    if ( g_repgame_flat_world ) {
        pr_debug( "Flat world: %s", world_name );
    }
    return g_repgame_flat_world;
}

void MapStorage::init( const char *world_name ) {
    char *dir = getRepGamePath( );
    snprintf( this->map_name, MAP_NAME_MAX_LENGTH, "%s%s%s", dir, REPGAME_PATH_DIVIDOR, world_name );
    pr_debug( "Loading map from:%s", this->map_name );
    if ( repgame_block_world( ) ) {
        // Block world creates no map dir and touches no files.
        free( dir );
        return;
    }
    mkdir_p( this->map_name );
    pr_debug( "Map dir ready:%s", this->map_name );
#if defined( REPGAME_MAP_SQLITE )
    // No loose-file fallback: on a sqlite build nothing may write the old
    // <kind>_<x>_<y>_<z> format, so an unopenable world.db is fatal.
    if ( this->sqlite_open( true ) ) {
        pr_debug( "Map backend: sqlite world.db" );
    } else {
        repgame_fatal( );
    }
#else
    pr_debug( "Map backend: loose files" );
#endif
    free( dir );
}

MapStorage::~MapStorage( ) {
#if defined( REPGAME_MAP_SQLITE )
    this->sqlite_close( );
#endif
}

void MapStorage::init_readonly( const char *map_dir ) {
    snprintf( this->map_name, MAP_NAME_MAX_LENGTH, "%s", map_dir );
#if defined( REPGAME_MAP_SQLITE )
    // world.db present -> read it through the sqlite backend; a dir of
    // loose files (or a non-sqlite build) keeps the file backend.
    char db_path[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( db_path, sizeof( db_path ), "%s/world.db", this->map_name );
    struct stat st;
    if ( stat( db_path, &st ) == 0 ) {
        this->sqlite_open( false );
    }
#endif
}

#if defined( REPGAME_MAP_SQLITE )
namespace {
// RAII lock for db_mutex — the member is pthread_mutex_t (std::mutex's
// <mutex> header breaks nvcc TUs that include map_storage.hpp).
struct DbLock {
    explicit DbLock( pthread_mutex_t &m ) : m( m ) {
        pthread_mutex_lock( &this->m );
    }
    ~DbLock( ) {
        pthread_mutex_unlock( &this->m );
    }
    pthread_mutex_t &m;
};
} // namespace

void MapStorage::sqlite_close( ) {
    if ( this->db == nullptr ) {
        return;
    }
    // A still-open batch means the object died mid-transaction: roll it
    // back rather than commit a torn group.
    if ( this->db_txn_depth > 0 ) {
        this->sqlite_exec( "ROLLBACK" );
        this->db_txn_depth = 0;
    }
    sqlite3_stmt *stmts[] = { this->stmt_get,  this->stmt_put,       this->stmt_del,  this->stmt_exists,
                              this->stmt_enum, this->stmt_kind_enum, this->stmt_size, this->stmt_peek };
    for ( sqlite3_stmt *st : stmts ) {
        sqlite3_finalize( st );
    }
    this->stmt_get = this->stmt_put = this->stmt_del = this->stmt_exists = nullptr;
    this->stmt_enum = this->stmt_kind_enum = this->stmt_size = this->stmt_peek = nullptr;
    sqlite3_close( this->db );
    this->db = nullptr;
}

int MapStorage::sqlite_exec( const char *sql ) {
    char *err = nullptr;
    const int rc = sqlite3_exec( this->db, sql, nullptr, nullptr, &err );
    if ( rc != SQLITE_OK ) {
        pr_debug( "sqlite exec failed (%d): %s [%s]", rc, err != nullptr ? err : "?", sql );
        sqlite3_free( err );
    }
    return rc == SQLITE_OK;
}

int MapStorage::sqlite_open( bool create ) {
    char db_path[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( db_path, sizeof( db_path ), "%s/world.db", this->map_name );
    const int flags = create ? ( SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX )
                             : ( SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX );
    if ( sqlite3_open_v2( db_path, &this->db, flags, nullptr ) != SQLITE_OK ) {
        pr_debug( "sqlite open failed for %s: %s", db_path,
                  this->db != nullptr ? sqlite3_errmsg( this->db ) : "?" );
        sqlite3_close( this->db );
        this->db = nullptr;
        return 0;
    }
    if ( create ) {
        // WAL gives crash-safe commits (checksums + atomic commit
        // records); synchronous=NORMAL is durable against an app crash —
        // only an OS crash/power loss can lose the last commit, and the
        // loose files were never fsync'd at all. Writer-only pragmas: a
        // READONLY open doesn't set them.
        this->sqlite_exec( "PRAGMA journal_mode=WAL" );
        this->sqlite_exec( "PRAGMA synchronous=NORMAL" );
        this->sqlite_exec( "PRAGMA busy_timeout=5000" );
        // One row per per-chunk record: (kind, chunk coords) -> the exact
        // bytes the loose file held, so every codec (v1 magic, record_size
        // prefix, legacy decode) reads identical payloads. WITHOUT ROWID
        // clusters rows by the PK instead of keeping a separate index.
        if ( !this->sqlite_exec( "CREATE TABLE IF NOT EXISTS entries("
                                 "kind INTEGER NOT NULL, x INTEGER NOT NULL, y INTEGER NOT NULL, z INTEGER NOT NULL,"
                                 "data BLOB NOT NULL, PRIMARY KEY(kind, x, y, z)) WITHOUT ROWID" ) ) {
            sqlite3_close( this->db );
            this->db = nullptr;
            return 0;
        }
    }
    {
        // Prepared for readonly opens too — the write stmts would simply
        // fail at step time on a READONLY handle, and nothing calls them.
        static const char *const sqls[] = {
            "SELECT data FROM entries WHERE kind=?1 AND x=?2 AND y=?3 AND z=?4",
            "INSERT OR REPLACE INTO entries(kind,x,y,z,data) VALUES(?1,?2,?3,?4,?5)",
            "DELETE FROM entries WHERE kind=?1 AND x=?2 AND y=?3 AND z=?4",
            "SELECT 1 FROM entries WHERE kind=?1 AND x=?2 AND y=?3 AND z=?4",
            "SELECT kind,x,y,z FROM entries",
            "SELECT x,y,z FROM entries WHERE kind=?1",
            "SELECT length(data) FROM entries WHERE kind=?1 AND x=?2 AND y=?3 AND z=?4",
            "SELECT substr(data,1,4) FROM entries WHERE kind=?1 AND x=?2 AND y=?3 AND z=?4",
        };
        sqlite3_stmt **slots[] = { &this->stmt_get,  &this->stmt_put,       &this->stmt_del,  &this->stmt_exists,
                                   &this->stmt_enum, &this->stmt_kind_enum, &this->stmt_size, &this->stmt_peek };
        for ( size_t i = 0; i < sizeof( sqls ) / sizeof( sqls[ 0 ] ); i++ ) {
            if ( sqlite3_prepare_v2( this->db, sqls[ i ], -1, slots[ i ], nullptr ) != SQLITE_OK ) {
                pr_debug( "sqlite prepare failed: %s", sqlite3_errmsg( this->db ) );
                sqlite3_close( this->db );
                this->db = nullptr;
                return 0;
            }
        }
    }
    if ( create ) {
        this->migrate_loose_files( );
    }
    return 1;
}

// Imports every loose <kind>_<x>_<y>_<z> file into world.db, then unlinks
// the originals — the one-shot upgrade for file-era worlds. The inserts
// commit (batch) before any file is unlinked, so a mid-import crash just
// re-imports next open.
void MapStorage::migrate_loose_files( ) {
    DIR *d = opendir( this->map_name );
    if ( d == nullptr ) {
        return;
    }
    std::vector<std::string> names;
    struct dirent *entry;
    while ( ( entry = readdir( d ) ) != nullptr ) {
        int kind = 0;
        glm::ivec3 pos;
        if ( parse_kind_filename( entry->d_name, kind, pos ) ) {
            names.emplace_back( entry->d_name );
        }
    }
    closedir( d );
    if ( names.empty( ) ) {
        return;
    }
    int migrated = 0;
    std::vector<std::string> imported;
    this->batch_begin( );
    for ( const std::string &name : names ) {
        int kind = 0;
        glm::ivec3 pos;
        if ( !parse_kind_filename( name.c_str( ), kind, pos ) ) {
            continue;
        }
        char path[ CHUNK_NAME_MAX_LENGTH ];
        snprintf( path, sizeof( path ), "%s/%s", this->map_name, name.c_str( ) );
        std::vector<unsigned char> buf;
        if ( !slurp_file( path, buf ) ) {
            continue;
        }
        {
            DbLock lock( this->db_mutex );
            sqlite3_stmt *st = this->stmt_put;
            sqlite3_bind_int( st, 1, kind );
            sqlite3_bind_int( st, 2, pos.x );
            sqlite3_bind_int( st, 3, pos.y );
            sqlite3_bind_int( st, 4, pos.z );
            sqlite3_bind_blob( st, 5, buf.data(), static_cast<int>( buf.size() ), SQLITE_TRANSIENT );
            const int rc = sqlite3_step( st );
            sqlite3_reset( st );
            if ( rc == SQLITE_DONE ) {
                imported.push_back( path );
                this->index_insert( kind, pos );
                migrated++;
            } else {
                pr_debug( "sqlite migrate failed for %s: %s", path, sqlite3_errmsg( this->db ) );
            }
        }
    }
    this->batch_end( );
    for ( const std::string &p : imported ) {
        remove( p.c_str( ) );
    }
    pr_debug( "sqlite: imported %d loose files into world.db under %s", migrated, this->map_name );
}
#endif // REPGAME_MAP_SQLITE

#if defined( REPGAME_MAP_STORAGE )
// ---- Per-kind blob IO ------------------------------------------------------
// The two backends share this interface: reads return the stored bytes,
// writes take bytes and make them durable. Decode/encode code is
// backend-agnostic and never touches a stream.

int MapStorage::read_kind_blob( int kind, const glm::ivec3 &pos, std::vector<unsigned char> &out ) {
    out.clear( );
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_get;
        sqlite3_bind_int( st, 1, kind );
        sqlite3_bind_int( st, 2, pos.x );
        sqlite3_bind_int( st, 3, pos.y );
        sqlite3_bind_int( st, 4, pos.z );
        int got = 0;
        if ( sqlite3_step( st ) == SQLITE_ROW ) {
            const int n = sqlite3_column_bytes( st, 0 );
            const unsigned char *p = static_cast<const unsigned char *>( sqlite3_column_blob( st, 0 ) );
            if ( n > 0 && p != nullptr ) {
                out.assign( p, p + n );
            }
            got = 1;
        }
        sqlite3_reset( st );
        return got;
    }
#endif
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, KIND_FILE_ROOT[ kind ], this->map_name, pos.x, pos.y, pos.z );
    return slurp_file( file_name, out );
}

int MapStorage::store_kind_blob( int kind, const glm::ivec3 &pos, const void *data, size_t len ) {
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_put;
        sqlite3_bind_int( st, 1, kind );
        sqlite3_bind_int( st, 2, pos.x );
        sqlite3_bind_int( st, 3, pos.y );
        sqlite3_bind_int( st, 4, pos.z );
        sqlite3_bind_blob( st, 5, data, static_cast<int>( len ), SQLITE_TRANSIENT );
        if ( sqlite3_step( st ) != SQLITE_DONE ) {
            sqlite3_reset( st );
            pr_debug( "sqlite write failed kind=%d pos=(%d,%d,%d): %s", kind, pos.x, pos.y, pos.z,
                      sqlite3_errmsg( this->db ) );
            return 0;
        }
        sqlite3_reset( st );
        this->index_insert( kind, pos );
        return 1;
    }
#endif
    // No writable backend (readonly open, or a build without sqlite):
    // nothing may write the loose <kind>_<x>_<y>_<z> format anymore.
    pr_debug( "store_kind_blob: no backend for kind=%d pos=(%d,%d,%d)", kind, pos.x, pos.y, pos.z );
    ( void )data;
    ( void )len;
    return 0;
}

void MapStorage::remove_kind( int kind, const glm::ivec3 &pos ) {
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_del;
        sqlite3_bind_int( st, 1, kind );
        sqlite3_bind_int( st, 2, pos.x );
        sqlite3_bind_int( st, 3, pos.y );
        sqlite3_bind_int( st, 4, pos.z );
        sqlite3_step( st );
        sqlite3_reset( st );
        this->index_remove( kind, pos );
        return;
    }
#endif
    // Without sqlite there is no record store to delete from — loose
    // files are never touched (a legacy dir opens read-only).
    ( void )kind;
    ( void )pos;
}

bool MapStorage::peek_kind_u32( int kind, const glm::ivec3 &pos, uint32_t &out ) {
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_peek;
        sqlite3_bind_int( st, 1, kind );
        sqlite3_bind_int( st, 2, pos.x );
        sqlite3_bind_int( st, 3, pos.y );
        sqlite3_bind_int( st, 4, pos.z );
        bool got = false;
        if ( sqlite3_step( st ) == SQLITE_ROW && sqlite3_column_bytes( st, 0 ) == 4 ) {
            memcpy( &out, sqlite3_column_blob( st, 0 ), sizeof( uint32_t ) );
            got = true;
        }
        sqlite3_reset( st );
        return got;
    }
#endif
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, KIND_FILE_ROOT[ kind ], this->map_name, pos.x, pos.y, pos.z );
    FILE *f = fopen( file_name, MAP_READ_MODE );
    if ( f == nullptr ) {
        return false;
    }
    const bool got = fread( &out, sizeof( uint32_t ), 1, f ) == 1;
    fclose( f );
    return got;
}

int MapStorage::enumerate_kind( int kind, std::vector<glm::ivec3> &out ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    int n = 0;
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_kind_enum;
        sqlite3_bind_int( st, 1, kind );
        while ( sqlite3_step( st ) == SQLITE_ROW ) {
            out.emplace_back( sqlite3_column_int( st, 0 ), sqlite3_column_int( st, 1 ), sqlite3_column_int( st, 2 ) );
            n++;
        }
        sqlite3_reset( st );
        return n;
    }
#endif
    DIR *d = opendir( this->map_name );
    if ( d == nullptr ) {
        return 0;
    }
    struct dirent *entry;
    while ( ( entry = readdir( d ) ) != nullptr ) {
        int k = 0;
        glm::ivec3 pos;
        if ( parse_kind_filename( entry->d_name, k, pos ) && k == kind ) {
            out.push_back( pos );
            n++;
        }
    }
    closedir( d );
    return n;
}

long MapStorage::stored_size( int kind, const glm::ivec3 &pos ) {
    if ( world_storage_off( ) ) {
        return -1;
    }
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_size;
        sqlite3_bind_int( st, 1, kind );
        sqlite3_bind_int( st, 2, pos.x );
        sqlite3_bind_int( st, 3, pos.y );
        sqlite3_bind_int( st, 4, pos.z );
        long size = -1;
        if ( sqlite3_step( st ) == SQLITE_ROW ) {
            size = static_cast<long>( sqlite3_column_int64( st, 0 ) );
        }
        sqlite3_reset( st );
        return size;
    }
#endif
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, KIND_FILE_ROOT[ kind ], this->map_name, pos.x, pos.y, pos.z );
    struct stat st;
    return stat( file_name, &st ) == 0 ? static_cast<long>( st.st_size ) : -1;
}

int MapStorage::write_kind_blob( int kind, const glm::ivec3 &pos, const void *data, size_t len ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    return this->store_kind_blob( kind, pos, data, len );
}

void MapStorage::batch_begin( ) {
#if defined( REPGAME_MAP_SQLITE )
    if ( this->db == nullptr ) {
        return;
    }
    pthread_mutex_lock( &this->db_mutex );
    if ( this->db_txn_depth++ == 0 ) {
        this->sqlite_exec( "BEGIN IMMEDIATE" );
    }
#endif
}

void MapStorage::batch_end( ) {
#if defined( REPGAME_MAP_SQLITE )
    if ( this->db == nullptr ) {
        return;
    }
    if ( --this->db_txn_depth == 0 ) {
        this->sqlite_exec( "COMMIT" );
    }
    pthread_mutex_unlock( &this->db_mutex );
#endif
}
#endif // REPGAME_MAP_STORAGE

char *MapStorage::get_map_name( ) {
    return this->map_name;
}

// Chunk edit file header (v1): 'RGCK' as a little-endian u32. It can't
// collide with a legacy file's leading record_size (8..14).
#define CHUNK_EDITS_MAGIC 0x4B434752u
#define CHUNK_EDITS_VERSION 1u

#if defined( REPGAME_MAP_STORAGE )
// ---- Save-file index ----------------------------------------------------
// Chunk coords packed into one u64 index key: x/z get 22 bits (~±2M
// chunks), y 20 bits (~±512K) — far past any real world. Two's-complement
// low bits keep negative coords distinct.
static inline uint64_t pack_chunk_coords( int x, int y, int z ) {
    return ( ( uint64_t )( x & 0x3FFFFF ) << 42 ) |
           ( ( uint64_t )( y & 0xFFFFF ) << 22 ) |
           ( ( uint64_t )( z & 0x3FFFFF ) );
}

static inline glm::ivec3 unpack_chunk_coords( uint64_t p ) {
    int x = ( int )( ( p >> 42 ) & 0x3FFFFF );
    int y = ( int )( ( p >> 22 ) & 0xFFFFF );
    int z = ( int )( p & 0x3FFFFF );
    if ( x & 0x200000 ) x -= 0x400000; // sign-extend 22-bit fields
    if ( z & 0x200000 ) z -= 0x400000;
    if ( y & 0x80000 ) y -= 0x100000; // 20-bit y
    return glm::ivec3( x, y, z );
}

void MapStorage::index_insert( int kind, const glm::ivec3 &pos ) {
    if ( this->file_index_enabled ) {
        this->file_index[ kind ].insert( pack_chunk_coords( pos.x, pos.y, pos.z ) );
    }
}

void MapStorage::index_remove( int kind, const glm::ivec3 &pos ) {
    if ( this->file_index_enabled ) {
        this->file_index[ kind ].erase( pack_chunk_coords( pos.x, pos.y, pos.z ) );
    }
}

bool MapStorage::index_miss( int kind, const glm::ivec3 &pos ) {
    if ( !this->file_index_enabled ||
         this->file_index[ kind ].count( pack_chunk_coords( pos.x, pos.y, pos.z ) ) ) {
        return false;
    }
    this->file_index_short_circuits++;
    return true;
}

void MapStorage::enable_file_index( ) {
    if ( this->file_index_enabled || world_storage_off( ) ) {
        return;
    }
    this->file_index_enabled = true;
    size_t indexed = 0;
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        // One SELECT instead of a readdir — every stored row is a blob.
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_enum;
        while ( sqlite3_step( st ) == SQLITE_ROW ) {
            const int kind = sqlite3_column_int( st, 0 );
            if ( kind >= 0 && kind < MAP_FILE_KIND_COUNT ) {
                this->file_index[ kind ].insert(
                    pack_chunk_coords( sqlite3_column_int( st, 1 ), sqlite3_column_int( st, 2 ),
                                       sqlite3_column_int( st, 3 ) ) );
                indexed++;
            }
        }
        sqlite3_reset( st );
        pr_debug( "file index: %zu rows in %s/world.db", indexed, this->map_name );
        return;
    }
#endif
    DIR *dir = opendir( this->map_name );
    if ( dir == NULL ) {
        pr_debug( "file index: opendir failed for %s", this->map_name );
        return;
    }
    struct dirent *entry;
    while ( ( entry = readdir( dir ) ) != NULL ) {
        int kind = 0;
        glm::ivec3 pos;
        if ( parse_kind_filename( entry->d_name, kind, pos ) ) {
            this->file_index[ kind ].insert( pack_chunk_coords( pos.x, pos.y, pos.z ) );
            indexed++;
        }
    }
    closedir( dir );
    pr_debug( "file index: %zu files under %s", indexed, this->map_name );
}

void MapStorage::file_index_counts( size_t out[ MAP_FILE_KIND_COUNT ] ) const {
    for ( int i = 0; i < MAP_FILE_KIND_COUNT; i++ ) {
        out[ i ] = this->file_index[ i ].size( );
    }
}

int MapStorage::migrate_legacy_chunk_files( ) {
    if ( world_storage_off( ) || !this->file_index_enabled ) {
        return 0;
    }
    // Snapshot the keys first: persist_chunk_edits mutates the index as
    // files are rewritten (or removed for empty run lists).
    std::vector<uint64_t> keys( this->file_index[ MAP_FILE_CHUNK ].begin( ),
                                this->file_index[ MAP_FILE_CHUNK ].end( ) );
    int migrated = 0;
    for ( const uint64_t key : keys ) {
        const glm::ivec3 pos = unpack_chunk_coords( key );
        uint32_t head = 0;
        if ( !this->peek_kind_u32( MAP_FILE_CHUNK, pos, head ) || head == CHUNK_EDITS_MAGIC ) {
            continue; // already v1 (or unreadable — left for the loader)
        }
        // Legacy format: decode through the normal path, re-encode as v1.
        ChunkEdits edits;
        if ( this->load_chunk_edits( pos, edits ) != 1 ) {
            continue; // undecodable — leave it rather than clobber
        }
        this->persist_chunk_edits( pos, edits.runs );
        migrated++;
    }
    return migrated;
}

typedef struct __attribute__( ( packed ) ) {
    BlockState blockState;
    unsigned int num;
} STORAGE_TYPE;

void MapStorage::persist_chunk_edits( const glm::ivec3 &chunk_offset, const std::vector<ChunkEditRun> &runs ) {
    if ( world_storage_off( ) ) {
        return;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return;
    }
    if ( runs.empty( ) ) {
        this->remove_kind( MAP_FILE_CHUNK, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 4 * sizeof( uint32_t ) + runs.size( ) * ( 2 * sizeof( uint32_t ) + sizeof( BlockState ) ) );
    const uint32_t magic = CHUNK_EDITS_MAGIC;
    const uint32_t version = CHUNK_EDITS_VERSION;
    const uint32_t record_size = sizeof( BlockState );
    const uint32_t run_count = static_cast<uint32_t>( runs.size( ) );
    w.put( magic );
    w.put( version );
    w.put( record_size );
    w.put( run_count );
    for ( const ChunkEditRun &run : runs ) {
        w.put( run.start );
        w.put( run.count );
        w.put( run.state );
    }
    this->store_kind_blob( MAP_FILE_CHUNK, chunk_offset, w.data.data( ), w.data.size( ) );
}

void MapStorage::persist_chunk_legacy( const glm::ivec3 &chunk_offset, const BlockState *blocks ) {
    BlobWriter w;
    const uint32_t storage_type_size = sizeof( STORAGE_TYPE );
    w.put( storage_type_size );
    STORAGE_TYPE persist_data;
    unsigned int num_same_blocks = 0;
    BlockState previousBlockState = { BLOCK_STATE_LAST_BLOCK_ID };
    for ( int i = 0; i < CHUNK_BLOCK_SIZE; i++ ) {
        const BlockState blockState = blocks[ i ];
        if ( BlockStates_equal( blockState, previousBlockState ) ) {
            num_same_blocks++;
        } else {
            if ( num_same_blocks > 0 ) {
                persist_data.blockState = previousBlockState;
                persist_data.num = num_same_blocks;
                w.put( persist_data );
            }
            previousBlockState = blockState;
            num_same_blocks = 1;
        }
    }
    persist_data.blockState = previousBlockState;
    persist_data.num = num_same_blocks;
    w.put( persist_data );
    // Test-only writer, but the index must still learn about the record.
    this->store_kind_blob( MAP_FILE_CHUNK, chunk_offset, w.data.data( ), w.data.size( ) );
}

namespace {
// Legacy (pre-v1) chunk files: a u32 record_size, then CHUNK_BLOCK_SIZE
// cells' worth of RLE records { state record (record_size-4 bytes),
// u32 num }. Decodes straight into edit runs: cells holding LAST_BLOCK_ID
// or LEGACY_EMPTY_BLOCK_ID are holes (server diff files) and skipped; a
// file with no holes is a client full snapshot. Kept self-contained so the
// whole branch can be deleted once old saves are gone.
int load_chunk_edits_legacy( BlobReader &r, const char *file_name, const uint32_t storage_type_size, ChunkEdits &out ) {
    out.needs_rewrite = true;
    out.legacy_snapshot = true;
    if ( storage_type_size <= sizeof( unsigned int ) || storage_type_size > 64 ) {
        return -1;
    }
    const bool loading_old_format_chunk = storage_type_size != sizeof( STORAGE_TYPE );
    if ( loading_old_format_chunk ) {
        pr_debug( "Loaded old chunk:%s with state size:%u expected:%d", file_name, storage_type_size, ( int )sizeof( STORAGE_TYPE ) );
    }

    int storage_block_state_size = storage_type_size - sizeof( unsigned int );
    // The rest of the blob is the record array: decode however many
    // complete records it holds (a full snapshot is CHUNK_BLOCK_SIZE
    // cells; server diff files are tiny).
    const unsigned int persist_data_length =
        ( unsigned int )( r.records( storage_type_size ) < ( size_t )CHUNK_BLOCK_SIZE ? r.records( storage_type_size )
                                                                                    : ( size_t )CHUNK_BLOCK_SIZE );
    const char *persist_data = reinterpret_cast<const char *>( r.at( ) );

    if ( storage_block_state_size > ( int )sizeof( BlockState ) ) {
        pr_debug( "Smaller" );
        storage_block_state_size = ( int )sizeof( BlockState );
    }

    uint32_t block_index = 0;
    int overflow = 0;
    for ( unsigned int i = 0; i < persist_data_length && !overflow; i++ ) {
        const char *block_storage = &persist_data[ i * storage_type_size ];
        unsigned int block_storage_num;
        memcpy( &block_storage_num, &persist_data[ ( ( i + 1 ) * storage_type_size ) - sizeof( unsigned int ) ], sizeof( block_storage_num ) );
        if ( block_index >= CHUNK_BLOCK_SIZE ) {
            overflow = 1;
            break;
        }
        if ( block_storage_num > CHUNK_BLOCK_SIZE - block_index ) {
            // A corrupt run length would silently truncate the decode: callers
            // re-encode edits.runs on the next persist, so returning a partial
            // cell map would rewrite the file with only this prefix and lose
            // the tail. Refuse it instead — the file stays on disk.
            pr_debug( "Chunk %s run length %u overflows at cell %u; refusing partial decode",
                      file_name, block_storage_num, block_index );
            return -1;
        }
        if ( block_storage_num == 0 ) {
            continue;
        }

        BlockState blockState = BLOCK_STATE_AIR;
        memset( &blockState, 0, sizeof( BlockState ) );
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
        if ( blockState.display_id == LEGACY_EMPTY_BLOCK_ID ) {
            blockState.display_id = blockState.id;
        }
        const bool hole = blockState.id == LAST_BLOCK_ID || blockState.id == LEGACY_EMPTY_BLOCK_ID;
        if ( hole ) {
            // Diff-style hole: the cell is natural, no edit emitted.
            out.legacy_snapshot = false;
        } else {
            if ( blockState.id >= LAST_BLOCK_ID ) {
                pr_debug( "Got strange block in %s :%d", file_name, blockState.id );
                blockState = BLOCK_STATE_AIR;
            }
            if ( !out.runs.empty( ) ) {
                ChunkEditRun &prev = out.runs.back( );
                if ( prev.start + prev.count == block_index && BlockStates_equal( prev.state, blockState ) ) {
                    prev.count += block_storage_num;
                    block_index += block_storage_num;
                    continue;
                }
            }
            out.runs.push_back( { block_index, block_storage_num, blockState } );
        }
        block_index += block_storage_num;
    }
    if ( block_index != CHUNK_BLOCK_SIZE ) {
        pr_debug( "Didn't get enough blocks load:%s", file_name );
    }
    return 1;
}
} // namespace

int MapStorage::load_chunk_edits( const glm::ivec3 &chunk_offset, ChunkEdits &out ) {
    out = ChunkEdits{ };
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return 0;
    }
    if ( !this->check_if_chunk_exists( chunk_offset ) ) {
        return 0;
    }
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_CHUNK, this->map_name, chunk_offset.x, chunk_offset.y, chunk_offset.z );
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_CHUNK, chunk_offset, blob ) ) {
        pr_debug( "open failed for chunk:%s", file_name );
        return -1;
    }
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t head = 0;
    if ( !r.get( head ) ) {
        return -1;
    }
    if ( head != CHUNK_EDITS_MAGIC ) {
        // `head` is the legacy file's record_size.
        return load_chunk_edits_legacy( r, file_name, head, out );
    }
    uint32_t version = 0, record_size = 0, run_count = 0;
    if ( !r.get( version ) || version != CHUNK_EDITS_VERSION ||
         !r.get( record_size ) || record_size == 0 || record_size > 4096 ||
         !r.get( run_count ) || run_count > CHUNK_BLOCK_SIZE ) {
        return -1;
    }
    if ( record_size != sizeof( BlockState ) ) {
        out.needs_rewrite = true;
    }
    std::vector<char> raw( record_size );
    uint32_t prev_end = 0;
    for ( uint32_t i = 0; i < run_count; i++ ) {
        uint32_t start = 0, count = 0;
        BlockState state = BLOCK_STATE_AIR;
        memset( &state, 0, sizeof( BlockState ) );
        if ( !r.get( start ) || !r.get( count ) || !r.get( raw.data( ), record_size ) ||
             count == 0 || start < prev_end || start + count > CHUNK_BLOCK_SIZE ) {
            out = ChunkEdits{ };
            return -1;
        }
        memcpy( &state, raw.data( ), record_size <= sizeof( BlockState ) ? record_size : sizeof( BlockState ) );
        if ( state.id >= LAST_BLOCK_ID ) {
            pr_debug( "Got strange block in %s :%d", file_name, state.id );
            state = BLOCK_STATE_AIR;
        }
        out.runs.push_back( { start, count, state } );
        prev_end = start + count;
    }
    return 1;
}

void MapStorage::persist_chunk( const Chunk &chunk ) {
    if ( world_storage_off( ) ) {
        return;
    }
    this->batch_begin( ); // one sqlite txn for the chunk + its sidecars
    if ( chunk.dirty ) {
        if ( !chunk.edits_read_failed ) {
            // Only the cells runtime edits touched get written; everything
            // else regenerates. Adjacent edited cells sharing a state merge
            // into one run.
            std::vector<ChunkEditRun> runs;
            if ( chunk.edited != nullptr ) {
                for ( int i = 0; i < CHUNK_BLOCK_SIZE; ) {
                    if ( !chunk.is_edited( i ) ) {
                        i++;
                        continue;
                    }
                    const int start = i;
                    const BlockState state = chunk.cells != nullptr ? chunk.state_at( i ) : BLOCK_STATE_AIR;
                    i++;
                    while ( i < CHUNK_BLOCK_SIZE && chunk.is_edited( i ) &&
                            BlockStates_equal( chunk.cells != nullptr ? chunk.state_at( i ) : BLOCK_STATE_AIR, state ) ) {
                        i++;
                    }
                    runs.push_back( { static_cast<uint32_t>( start ), static_cast<uint32_t>( i - start ), state } );
                }
            }
            this->persist_chunk_edits( chunk.chunk_pos, runs );
        }
        if ( chunk.containers.chests_loaded ) {
            // Chest sidecar file is keyed by world block position.
            ChestContentsMap chest_world_map;
            const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
            for ( const auto &pair : chunk.containers.chests ) {
                const int index = pair.first;
                const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                               ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                               ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
                chest_world_map[ chunk_origin + internal_pos ] = pair.second;
            }
            this->persist_chests( chunk.chunk_pos, chest_world_map );
        }
        if ( chunk.containers.furnaces_loaded ) {
            // Furnace sidecar file, same world-pos keying as chests.
            FurnaceContentsMap furnace_world_map;
            const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
            for ( const auto &pair : chunk.containers.furnaces ) {
                const int index = pair.first;
                const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                               ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                               ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
                furnace_world_map[ chunk_origin + internal_pos ] = pair.second;
            }
            this->persist_furnaces( chunk.chunk_pos, furnace_world_map );
        }
        if ( chunk.containers.signs_loaded ) {
            // Sign sidecar file, same world-pos keying as chests.
            SignTextMap sign_world_map;
            const glm::ivec3 chunk_origin = chunk.chunk_pos * CHUNK_SIZE_I;
            for ( const auto &pair : chunk.containers.signs ) {
                const int index = pair.first;
                const glm::ivec3 internal_pos( ( ( index / CHUNK_SIZE_INTERNAL_Z ) % CHUNK_SIZE_INTERNAL_X ) - 1,
                                               ( index / ( CHUNK_SIZE_INTERNAL_Z * CHUNK_SIZE_INTERNAL_X ) ) - 1,
                                               ( index % CHUNK_SIZE_INTERNAL_Z ) - 1 );
                sign_world_map[ chunk_origin + internal_pos ] = pair.second;
            }
            this->persist_signs( chunk.chunk_pos, sign_world_map );
        }
    }
    this->batch_end( );
}

int MapStorage::check_if_chunk_exists( const glm::ivec3 &chunk_offset ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( this->file_index_enabled ) {
        // The index answers without a stat/query — this is the hot path
        // for server CHUNK_DIFF_REQUEST boxes.
        this->file_index_short_circuits++;
        return this->file_index[ MAP_FILE_CHUNK ].count( pack_chunk_coords( chunk_offset.x, chunk_offset.y, chunk_offset.z ) ) ? 1 : 0;
    }
#if defined( REPGAME_MAP_SQLITE )
    if ( this->use_sqlite( ) ) {
        DbLock lock( this->db_mutex );
        sqlite3_stmt *st = this->stmt_exists;
        sqlite3_bind_int( st, 1, MAP_FILE_CHUNK );
        sqlite3_bind_int( st, 2, chunk_offset.x );
        sqlite3_bind_int( st, 3, chunk_offset.y );
        sqlite3_bind_int( st, 4, chunk_offset.z );
        const int exists = sqlite3_step( st ) == SQLITE_ROW;
        sqlite3_reset( st );
        return exists;
    }
#endif
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
    if ( world_storage_off( ) ) {
        return;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return;
    }
    if ( chests.empty( ) ) {
        // No chests left in this chunk — remove the record so deleted
        // chests don't resurrect on the next load.
        this->remove_kind( MAP_FILE_CHEST, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 2 * sizeof( uint32_t ) + chests.size( ) * sizeof( ChestFileRecord ) );
    const uint32_t record_size = sizeof( ChestFileRecord );
    const uint32_t num_chests = static_cast<uint32_t>( chests.size( ) );
    w.put( record_size );
    w.put( num_chests );
    for ( const auto &pair : chests ) {
        ChestFileRecord record;
        record.x = pair.first.x;
        record.y = pair.first.y;
        record.z = pair.first.z;
        memcpy( record.slots, pair.second.slots, sizeof( record.slots ) );
        w.put( record );
    }
    this->store_kind_blob( MAP_FILE_CHEST, chunk_offset, w.data.data( ), w.data.size( ) );
}

int MapStorage::load_chests( const glm::ivec3 &chunk_offset, ChestContentsMap &out ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return 0;
    }
    if ( this->index_miss( MAP_FILE_CHEST, chunk_offset ) ) {
        return 0;
    }
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_CHEST, chunk_offset, blob ) ) {
        return 0;
    }
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t record_size = 0;
    if ( !r.get( record_size ) ) {
        return 0;
    }
    uint32_t num_chests = 0;
    if ( !r.get( num_chests ) || num_chests > CHUNK_BLOCK_SIZE ) {
        return 0;
    }
    // Read whatever the file's record holds; older/newer builds may have a
    // different record size, so copy only the shared prefix per record.
    const int slot_prefix = offsetof( ChestFileRecord, slots );
    const size_t num_read = r.records( record_size ) < num_chests ? r.records( record_size ) : num_chests;
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = reinterpret_cast<const char *>( r.at( ) ) + i * record_size;
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
            if ( save_slot_id_is_empty( contents.slots[ s ].item_id ) ) {
                contents.slots[ s ].item_id = LAST_BLOCK_ID;
                contents.slots[ s ].quantity = 0;
            }
        }
        out[ pos ] = contents;
    }
    return 1;
}

// Furnace file layout: same envelope as the chest file — u32 record_size,
// u32 num_furnaces, then per furnace:
//   { i32 x, i32 y, i32 z, InventorySlot slots[FURNACE_INVENTORY_SLOTS],
//     i32 cook_progress, i32 burn_ticks }.
struct __attribute__( ( packed ) ) FurnaceFileRecord {
    int x;
    int y;
    int z;
    InventorySlot slots[ FURNACE_INVENTORY_SLOTS ];
    int cook_progress;
    int burn_ticks;
};

// A furnace with no items and no lit/progress state carries nothing worth
// a record — persist_furnaces drops these so emptied furnaces don't leave
// junk behind (and an all-empty map removes the file like chests).
static bool furnace_contents_empty( const FurnaceContents &fc ) {
    if ( fc.cook_progress != 0 || fc.burn_ticks != 0 ) {
        return false;
    }
    for ( int s = 0; s < FURNACE_INVENTORY_SLOTS; s++ ) {
        if ( fc.slots[ s ].item_id != LAST_BLOCK_ID && fc.slots[ s ].quantity > 0 ) {
            return false;
        }
    }
    return true;
}

void MapStorage::persist_furnaces( const glm::ivec3 &chunk_offset, const FurnaceContentsMap &furnaces ) {
    if ( world_storage_off( ) ) {
        return;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return;
    }
    uint32_t num_furnaces = 0;
    for ( const auto &pair : furnaces ) {
        if ( !furnace_contents_empty( pair.second ) ) {
            num_furnaces++;
        }
    }
    if ( num_furnaces == 0 ) {
        this->remove_kind( MAP_FILE_FURNACE, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 2 * sizeof( uint32_t ) + num_furnaces * sizeof( FurnaceFileRecord ) );
    const uint32_t record_size = sizeof( FurnaceFileRecord );
    w.put( record_size );
    w.put( num_furnaces );
    for ( const auto &pair : furnaces ) {
        if ( furnace_contents_empty( pair.second ) ) {
            continue;
        }
        FurnaceFileRecord record;
        record.x = pair.first.x;
        record.y = pair.first.y;
        record.z = pair.first.z;
        memcpy( record.slots, pair.second.slots, sizeof( record.slots ) );
        record.cook_progress = pair.second.cook_progress;
        record.burn_ticks = pair.second.burn_ticks;
        w.put( record );
    }
    this->store_kind_blob( MAP_FILE_FURNACE, chunk_offset, w.data.data( ), w.data.size( ) );
}

int MapStorage::load_furnaces( const glm::ivec3 &chunk_offset, FurnaceContentsMap &out ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return 0;
    }
    if ( this->index_miss( MAP_FILE_FURNACE, chunk_offset ) ) {
        return 0;
    }
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_FURNACE, chunk_offset, blob ) ) {
        return 0;
    }
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t record_size = 0;
    if ( !r.get( record_size ) ) {
        return 0;
    }
    uint32_t num_furnaces = 0;
    if ( !r.get( num_furnaces ) || num_furnaces > CHUNK_BLOCK_SIZE ) {
        return 0;
    }
    // Read whatever the file's record holds; older/newer builds may have a
    // different record size, so copy only the shared prefix per record.
    const int slot_prefix = offsetof( FurnaceFileRecord, slots );
    const int tail_prefix = offsetof( FurnaceFileRecord, cook_progress );
    const size_t num_read = r.records( record_size ) < num_furnaces ? r.records( record_size ) : num_furnaces;
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = reinterpret_cast<const char *>( r.at( ) ) + i * record_size;
        FurnaceContents contents; // default: empty + zeroed progress
        glm::ivec3 pos( 0 );
        memcpy( &pos, record, sizeof( glm::ivec3 ) <= (size_t)record_size ? sizeof( glm::ivec3 ) : (size_t)record_size );
        if ( record_size > (uint32_t)slot_prefix ) {
            int slot_bytes = record_size - slot_prefix;
            if ( slot_bytes > (int)sizeof( contents.slots ) ) {
                slot_bytes = (int)sizeof( contents.slots );
            }
            memcpy( contents.slots, record + slot_prefix, slot_bytes );
        }
        // Progress fields only exist in current-format records; partial
        // tails leave them zeroed rather than overreading the buffer.
        if ( record_size >= (uint32_t)( tail_prefix + 2 * sizeof( int ) ) ) {
            memcpy( &contents.cook_progress, record + tail_prefix, sizeof( contents.cook_progress ) );
            memcpy( &contents.burn_ticks, record + tail_prefix + sizeof( contents.cook_progress ), sizeof( contents.burn_ticks ) );
        }
        for ( int s = 0; s < FURNACE_INVENTORY_SLOTS; s++ ) {
            if ( save_slot_id_is_empty( contents.slots[ s ].item_id ) ) {
                contents.slots[ s ].item_id = LAST_BLOCK_ID;
                contents.slots[ s ].quantity = 0;
            }
        }
        out[ pos ] = contents;
    }
    return 1;
}

// Sign file layout: same envelope as the chest file — u32 record_size,
// u32 num_signs, then per sign a fixed SignFileRecord:
//   { i32 x, i32 y, i32 (world block pos), u8 line_len[4],
//     char text[4][SIGN_LINE_MAX] }.
// Lines hold printable ASCII only (sanitized on write and again on read).
struct __attribute__( ( packed ) ) SignFileRecord {
    int x;
    int y;
    int z;
    unsigned char line_len[ SIGN_LINES ];
    char text[ SIGN_LINES ][ SIGN_LINE_MAX ];
};

void MapStorage::persist_signs( const glm::ivec3 &chunk_offset, const SignTextMap &signs ) {
    if ( world_storage_off( ) ) {
        return;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return;
    }
    uint32_t num_signs = 0;
    for ( const auto &pair : signs ) {
        if ( !pair.second.empty( ) ) {
            num_signs++;
        }
    }
    if ( num_signs == 0 ) {
        // No text left in this chunk — remove the record so erased signs
        // don't resurrect on the next load.
        this->remove_kind( MAP_FILE_SIGN, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 2 * sizeof( uint32_t ) + num_signs * sizeof( SignFileRecord ) );
    const uint32_t record_size = sizeof( SignFileRecord );
    w.put( record_size );
    w.put( num_signs );
    for ( const auto &pair : signs ) {
        if ( pair.second.empty( ) ) {
            continue;
        }
        SignFileRecord record;
        memset( &record, 0, sizeof( record ) );
        record.x = pair.first.x;
        record.y = pair.first.y;
        record.z = pair.first.z;
        for ( int l = 0; l < SIGN_LINES; l++ ) {
            const size_t len = pair.second.lines[ l ].size( ) < SIGN_LINE_MAX ? pair.second.lines[ l ].size( ) : SIGN_LINE_MAX;
            record.line_len[ l ] = static_cast<unsigned char>( len );
            memcpy( record.text[ l ], pair.second.lines[ l ].data( ), len );
        }
        w.put( record );
    }
    this->store_kind_blob( MAP_FILE_SIGN, chunk_offset, w.data.data( ), w.data.size( ) );
}

int MapStorage::load_signs( const glm::ivec3 &chunk_offset, SignTextMap &out ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( !this->chunk_io_enabled.load( std::memory_order_relaxed ) ) {
        return 0;
    }
    if ( this->index_miss( MAP_FILE_SIGN, chunk_offset ) ) {
        return 0;
    }
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_SIGN, chunk_offset, blob ) ) {
        return 0;
    }
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t record_size = 0;
    if ( !r.get( record_size ) ) {
        return 0;
    }
    uint32_t num_signs = 0;
    if ( !r.get( num_signs ) || num_signs > CHUNK_BLOCK_SIZE ) {
        return 0;
    }
    // Read whatever the file's record holds; older/newer builds may have a
    // different record size, so copy only the shared prefix per record and
    // never index past it.
    const int text_prefix = offsetof( SignFileRecord, line_len );
    const size_t num_read = r.records( record_size ) < num_signs ? r.records( record_size ) : num_signs;
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = reinterpret_cast<const char *>( r.at( ) ) + i * record_size;
        SignText text; // default: all lines empty
        glm::ivec3 pos( 0 );
        memcpy( &pos, record, sizeof( glm::ivec3 ) <= (size_t)record_size ? sizeof( glm::ivec3 ) : (size_t)record_size );
        if ( record_size > (uint32_t)text_prefix + SIGN_LINES ) {
            const unsigned char *lens = reinterpret_cast<const unsigned char *>( record + text_prefix );
            const char *chars = record + text_prefix + SIGN_LINES;
            for ( int l = 0; l < SIGN_LINES; l++ ) {
                const int line_off = text_prefix + SIGN_LINES + l * SIGN_LINE_MAX;
                const int avail = static_cast<int>( record_size ) - line_off;
                if ( avail <= 0 ) {
                    break;
                }
                int len = lens[ l ];
                if ( len > SIGN_LINE_MAX || len > avail ) {
                    len = avail < SIGN_LINE_MAX ? avail : SIGN_LINE_MAX;
                }
                if ( len > 0 ) {
                    text.lines[ l ].assign( chars + l * SIGN_LINE_MAX, len );
                }
            }
        }
        sign_sanitize( text );
        if ( !text.empty( ) ) {
            out[ pos ] = text;
        }
    }
    return 1;
}

// Mob file layout: u32 record_size, u32 count, `count` MobFileRecords, then
// u32 removed_count + that many u8 natural-roll indices tombstoned by
// touched/killed naturals. Files written before net_id / the removed
// section existed still parse (net_id zero-fills, removed stays empty). An
// all-empty write deletes the file instead — nothing about the file's
// existence suppresses the deterministic roll anymore.
void MapStorage::persist_mobs( const glm::ivec3 &chunk_offset, const std::vector<MobFileRecord> &mobs, const std::vector<uint8_t> &removed ) {
    if ( world_storage_off( ) ) {
        return;
    }
    if ( mobs.empty( ) && removed.empty( ) ) {
        this->remove_kind( MAP_FILE_MOBS, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 3 * sizeof( uint32_t ) + mobs.size( ) * sizeof( MobFileRecord ) + removed.size( ) );
    const uint32_t record_size = sizeof( MobFileRecord );
    const uint32_t num_mobs = static_cast<uint32_t>( mobs.size( ) );
    w.put( record_size );
    w.put( num_mobs );
    for ( const MobFileRecord &record : mobs ) {
        w.put( record );
    }
    const uint32_t num_removed = static_cast<uint32_t>( removed.size( ) );
    w.put( num_removed );
    if ( num_removed > 0 ) {
        w.put( removed.data( ), num_removed );
    }
    this->store_kind_blob( MAP_FILE_MOBS, chunk_offset, w.data.data( ), w.data.size( ) );
}

int MapStorage::load_mobs( const glm::ivec3 &chunk_offset, std::vector<MobFileRecord> &out, std::vector<uint8_t> *removed ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( this->index_miss( MAP_FILE_MOBS, chunk_offset ) ) {
        return 0;
    }
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_MOBS, chunk_offset, blob ) ) {
        return 0;
    }
    out.clear( );
    if ( removed != nullptr ) {
        removed->clear( );
    }
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t record_size = 0;
    if ( !r.get( record_size ) ) {
        // Truncated/marker record: it exists, which is all the caller needs.
        return 1;
    }
    uint32_t num_mobs = 0;
    if ( !r.get( num_mobs ) || num_mobs > 65535 ) {
        return 1;
    }
    // Read whatever the file's record holds; older/newer builds may have a
    // different record size, so copy only the shared prefix per record
    // (shorter records zero-fill their tail, e.g. net_id on pre-net_id files).
    const size_t num_read = r.records( record_size ) < num_mobs ? r.records( record_size ) : num_mobs;
    const unsigned char *records = r.at( );
    r.skip( num_read * record_size );
    uint32_t num_removed = 0;
    if ( removed != nullptr && r.get( num_removed ) && num_removed <= 64 && r.left( ) >= num_removed ) {
        removed->assign( r.at( ), r.at( ) + num_removed );
        r.skip( num_removed );
    }
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = reinterpret_cast<const char *>( records ) + i * record_size;
        MobFileRecord mob = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0u, 0u, 0 };
        const size_t prefix = sizeof( MobFileRecord ) <= (size_t)record_size ? sizeof( MobFileRecord ) : (size_t)record_size;
        memcpy( &mob, record, prefix );
        if ( mob.type >= MOB_TYPE_COUNT ) {
            continue;
        }
        out.push_back( mob );
    }
    return 1;
}

// Item file layout: u32 record_size, u32 count, then `count` ItemFileRecords.
// An empty list removes the file — a missing file and "no items" are
// equivalent, and unlinking still clears stale records for a chunk whose
// items were all picked up.
void MapStorage::persist_items( const glm::ivec3 &chunk_offset, const std::vector<ItemFileRecord> &items ) {
    if ( world_storage_off( ) ) {
        return;
    }
    if ( items.empty( ) ) {
        this->remove_kind( MAP_FILE_ITEMS, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 2 * sizeof( uint32_t ) + items.size( ) * sizeof( ItemFileRecord ) );
    const uint32_t record_size = sizeof( ItemFileRecord );
    const uint32_t num_items = static_cast<uint32_t>( items.size( ) );
    w.put( record_size );
    w.put( num_items );
    for ( const ItemFileRecord &record : items ) {
        w.put( record );
    }
    this->store_kind_blob( MAP_FILE_ITEMS, chunk_offset, w.data.data( ), w.data.size( ) );
}

int MapStorage::load_items( const glm::ivec3 &chunk_offset, std::vector<ItemFileRecord> &out ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( this->index_miss( MAP_FILE_ITEMS, chunk_offset ) ) {
        return 0;
    }
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_ITEMS, chunk_offset, blob ) ) {
        return 0;
    }
    out.clear( );
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t record_size = 0;
    if ( !r.get( record_size ) || record_size == 0 ) {
        return 1;
    }
    uint32_t num_items = 0;
    if ( !r.get( num_items ) || num_items > 65535 ) {
        return 1;
    }
    const size_t num_read = r.records( record_size ) < num_items ? r.records( record_size ) : num_items;
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = reinterpret_cast<const char *>( r.at( ) ) + i * record_size;
        ItemFileRecord item = { 0, 0, 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0 };
        const size_t prefix = sizeof( ItemFileRecord ) <= (size_t)record_size ? sizeof( ItemFileRecord ) : (size_t)record_size;
        memcpy( &item, record, prefix );
        if ( !item_id_valid( item.item ) || item.count <= 0 ) {
            continue;
        }
        out.push_back( item );
    }
    return 1;
}

// Vehicle file layout: u32 record_size, u32 count, then `count`
// VehicleFileRecords. Same conventions as the items file — an empty list
// removes the file. The on-disk name stays carts_x_y_z (no migration).
void MapStorage::persist_vehicles( const glm::ivec3 &chunk_offset, const std::vector<VehicleFileRecord> &vehicles ) {
    if ( world_storage_off( ) ) {
        return;
    }
    if ( vehicles.empty( ) ) {
        this->remove_kind( MAP_FILE_CARTS, chunk_offset );
        return;
    }
    BlobWriter w;
    w.data.reserve( 2 * sizeof( uint32_t ) + vehicles.size( ) * sizeof( VehicleFileRecord ) );
    const uint32_t record_size = sizeof( VehicleFileRecord );
    const uint32_t num_vehicles = static_cast<uint32_t>( vehicles.size( ) );
    w.put( record_size );
    w.put( num_vehicles );
    for ( const VehicleFileRecord &record : vehicles ) {
        w.put( record );
    }
    this->store_kind_blob( MAP_FILE_CARTS, chunk_offset, w.data.data( ), w.data.size( ) );
}

int MapStorage::load_vehicles( const glm::ivec3 &chunk_offset, std::vector<VehicleFileRecord> &out ) {
    if ( world_storage_off( ) ) {
        return 0;
    }
    if ( this->index_miss( MAP_FILE_CARTS, chunk_offset ) ) {
        return 0;
    }
    std::vector<unsigned char> blob;
    if ( !this->read_kind_blob( MAP_FILE_CARTS, chunk_offset, blob ) ) {
        return 0;
    }
    out.clear( );
    BlobReader r{ blob.data( ), blob.size( ), 0 };
    uint32_t record_size = 0;
    if ( !r.get( record_size ) || record_size == 0 ) {
        return 1;
    }
    uint32_t num_vehicles = 0;
    if ( !r.get( num_vehicles ) || num_vehicles > 65535 ) {
        return 1;
    }
    const size_t num_read = r.records( record_size ) < num_vehicles ? r.records( record_size ) : num_vehicles;
    for ( size_t i = 0; i < num_read; i++ ) {
        const char *record = reinterpret_cast<const char *>( r.at( ) ) + i * record_size;
        VehicleFileRecord vehicle = { 0.0, 0.0, 0.0, 0.0f, 0.0f, 0.0f, 0.0f, 0u };
        const size_t prefix = sizeof( VehicleFileRecord ) <= (size_t)record_size ? sizeof( VehicleFileRecord ) : (size_t)record_size;
        memcpy( &vehicle, record, prefix );
        if ( !std::isfinite( vehicle.x ) || !std::isfinite( vehicle.y ) || !std::isfinite( vehicle.z ) || !vehicle_type_valid( vehicle.type ) ) {
            continue;
        }
        out.push_back( vehicle );
    }
    return 1;
}


#else // !REPGAME_MAP_STORAGE — WASM/Android persist no per-chunk state,
// so every per-chunk entry point is inert. player.dat/world.dat below
// stay live on every platform.

void MapStorage::persist_chunk( const Chunk & ) { }
int MapStorage::load_chunk_edits( const glm::ivec3 &, ChunkEdits &out ) {
    out = ChunkEdits{ };
    return 0;
}
void MapStorage::persist_chunk_edits( const glm::ivec3 &, const std::vector<ChunkEditRun> & ) { }
void MapStorage::persist_chunk_legacy( const glm::ivec3 &, const BlockState * ) { }
void MapStorage::persist_chests( const glm::ivec3 &, const ChestContentsMap & ) { }
int MapStorage::check_if_chunk_exists( const glm::ivec3 & ) { return 0; }
int MapStorage::load_chests( const glm::ivec3 &, ChestContentsMap & ) { return 0; }
void MapStorage::persist_furnaces( const glm::ivec3 &, const FurnaceContentsMap & ) { }
int MapStorage::load_furnaces( const glm::ivec3 &, FurnaceContentsMap & ) { return 0; }
void MapStorage::persist_signs( const glm::ivec3 &, const SignTextMap & ) { }
int MapStorage::load_signs( const glm::ivec3 &, SignTextMap & ) { return 0; }
void MapStorage::persist_mobs( const glm::ivec3 &, const std::vector<MobFileRecord> &, const std::vector<uint8_t> & ) { }
int MapStorage::load_mobs( const glm::ivec3 &, std::vector<MobFileRecord> &, std::vector<uint8_t> * ) { return 0; }
void MapStorage::persist_items( const glm::ivec3 &, const std::vector<ItemFileRecord> & ) { }
int MapStorage::load_items( const glm::ivec3 &, std::vector<ItemFileRecord> & ) { return 0; }
void MapStorage::persist_vehicles( const glm::ivec3 &, const std::vector<VehicleFileRecord> & ) { }
int MapStorage::load_vehicles( const glm::ivec3 &, std::vector<VehicleFileRecord> & ) { return 0; }
void MapStorage::enable_file_index( ) { }
int MapStorage::enumerate_kind( int, std::vector<glm::ivec3> & ) { return 0; }
long MapStorage::stored_size( int, const glm::ivec3 & ) { return -1; }
int MapStorage::write_kind_blob( int, const glm::ivec3 &, const void *, size_t ) { return 0; }
void MapStorage::batch_begin( ) { }
void MapStorage::batch_end( ) { }
int MapStorage::migrate_legacy_chunk_files( ) { return 0; }
void MapStorage::file_index_counts( size_t out[ MAP_FILE_KIND_COUNT ] ) const {
    memset( out, 0, MAP_FILE_KIND_COUNT * sizeof( size_t ) );
}

#endif // REPGAME_MAP_STORAGE

// Shared by the chest/furnace codecs and player.dat's slot reader.
static void sanitize_slots( InventorySlot *slots, int count ) {
    for ( int i = 0; i < count; i++ ) {
        InventorySlot &slot = slots[ i ];
        if ( save_slot_id_is_empty( slot.item_id ) ) {
            slot.item_id = LAST_BLOCK_ID;
            slot.quantity = 0;
        }
    }
}

int MapStorage::read_player_data( PlayerData &player_data ) {
    if ( repgame_block_world( ) ) {
        return 0;
    }
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_PLAYER_DATA, this->map_name );
    FILE *read_ptr = fopen( file_name, MAP_READ_MODE );
    if ( !read_ptr ) {
        pr_debug( "No player data:%s (%p)", file_name, read_ptr );
        return 0;
    }
    // The text format is a few KB; the cap just guards against garbage files.
    unsigned char buffer[ 64 * 1024 ];
    size_t persist_data_length = fread( buffer, 1, sizeof( buffer ), read_ptr );
    fclose( read_ptr );
    pr_debug( "Read player data:%s (%d bytes)", file_name, ( int )persist_data_length );
    if ( !player_data_parse( buffer, persist_data_length, player_data ) ) {
        pr_debug( "Warning, unrecognized player data:%s", file_name );
        return 0;
    }
    sanitize_slots( player_data.hotbar_inventory, HOTBAR_WIDTH * HOTBAR_HEIGHT );
    sanitize_slots( player_data.survival_inventory, SURVIVAL_INVENTORY_WIDTH * SURVIVAL_INVENTORY_HEIGHT );
    return 1;
}

void MapStorage::write_player_data( const PlayerData &player_data ) {
    if ( repgame_block_world( ) ) {
        return;
    }
    char file_name[ CHUNK_NAME_MAX_LENGTH ];
    snprintf( file_name, CHUNK_NAME_MAX_LENGTH, FILE_ROOT_PLAYER_DATA, this->map_name );
    FILE *write_ptr = fopen( file_name, "wb" );
    if ( !write_ptr ) {
        pr_debug( "Can't write player:%s (%p)", file_name, write_ptr );
        return;
    }
    const std::string text = player_data_serialize( player_data );
    fwrite( text.data( ), 1, text.size( ), write_ptr );
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
    if ( repgame_block_world( ) ) {
        return 0;
    }
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
    if ( repgame_block_world( ) ) {
        return;
    }
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
