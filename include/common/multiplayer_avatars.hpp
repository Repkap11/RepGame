#pragma once

#include <stdint.h>

#include "common/constants.hpp"
#include "common/mobs.hpp"

// Remote players render through the mob pipeline (MOB_PLAYER, MOB_AI_REMOTE):
// each connected player_id maps to a network-driven mob's uid.
class MultiplayerAvatars {
    MobManager *mobs;
    uint32_t uid_map[ MAX_CLIENT_FDS ];
    // Last received NET_PLAYER_FLAG_* per player (bit0: targetable —
    // survival && !no_clip). Stored for the mob-sync phase.
    uint8_t flags_map[ MAX_CLIENT_FDS ];
    // Last received eye position/height per player, for hostile-AI remote
    // targets (mob phase of entity sync).
    glm::dvec3 eye_pos_map[ MAX_CLIENT_FDS ];
    float eye_height_map[ MAX_CLIENT_FDS ];

  public:
    void init( MobManager *mob_manager );
    void add( unsigned int player_id );
    void update_position( int player_id, float x, float y, float z, const glm::mat4 &rotation, float eye_height, uint8_t flags );
    bool targetable( int player_id ) const;
    // Fill `out` with every connected remote player's targeting info.
    void fill_remote_targets( std::vector<MobRemoteTarget> &out ) const;
    void remove( unsigned int player_id );
    void cleanup( );
};
