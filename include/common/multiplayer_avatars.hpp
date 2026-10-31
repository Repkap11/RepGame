#pragma once

#include <stdint.h>

#include "common/constants.hpp"
#include "common/mobs.hpp"

// Remote players render through the mob pipeline (MOB_PLAYER, MOB_AI_REMOTE):
// each connected player_id maps to a network-driven mob's uid.
class MultiplayerAvatars {
    MobManager *mobs;
    uint32_t uid_map[ MAX_CLIENT_FDS ];

  public:
    void init( MobManager *mob_manager );
    void add( unsigned int player_id );
    void update_position( int player_id, float x, float y, float z, const glm::mat4 &rotation );
    void remove( unsigned int player_id );
    void cleanup( );
};
