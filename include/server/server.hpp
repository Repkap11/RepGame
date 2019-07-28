
#include <glm.hpp>

typedef enum { INVALID, PLAYER_LOCATION, BLOCK_UPDATE, CLIENT_INIT, SERVER_ACK } PacketType;

typedef struct {
    PacketType type;
    int player_id;
    union {
        struct {
            int x;
            int y;
            int z;
            int blockID;
        } block;
        struct {
            float x;
            float y;
            float z;
            float rotation[ 4 * 4 ];
        } player;
    } data;
} NetPacket;
