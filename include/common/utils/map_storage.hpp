#pragma once

class Chunk;
struct PlayerData;

#define FILE_ROOT_CHUNK "%s/chunk_%d_%d_%d"
#define FILE_ROOT_PLAYER_DATA "%s/player.dat"

struct PlayerData;

class MapStorage {
  public:
    void init( const char *world_name );
    void persist( const Chunk &chunk );
    int load( Chunk &chunk );
    int read_player_data( PlayerData &player_data );
    void write_player_data( const PlayerData &player_data );
};