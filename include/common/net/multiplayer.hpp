
#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/chunk.hpp"

void multiplayer_init( const char *hostname, int port );
void multiplayer_cleanup( );

void dostuff( int );

void server_set_block( int place, int block_x, int block_y, int block_z, BlockID blockID );

typedef enum { PLAYER_LOCATION, BLOCK_UPDATE, CLIENT_INIT } PacketType;

typedef struct {
    PacketType type;
    long sequence;
    int x;
    int y;
    int z;
    BlockID blockID;
} NetPacket;