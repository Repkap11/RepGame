#include "common/multiplayer_avatars.hpp"
#include "common/RepGame.hpp"

#include <string.h>

void MultiplayerAvatars::init( MobManager *mob_manager ) {
    this->mobs = mob_manager;
    memset( this->uid_map, 0, sizeof( this->uid_map ) );
    memset( this->flags_map, 0, sizeof( this->flags_map ) );
    for ( float &h : this->health_map ) {
        h = -1.0f;
    }
}

void MultiplayerAvatars::add( const unsigned int player_id ) {
    // A second add for a live id would orphan the old mob (rendered forever
    // at its last position) — remove it first.
    this->remove( player_id );
    this->uid_map[ player_id ] = this->mobs->spawn_remote( MOB_PLAYER );
    this->health_map[ player_id ] = -1.0f;
}

void MultiplayerAvatars::update_position( const int player_id, const float x, const float y, const float z, const glm::mat4 &rotation, const float eye_height, const uint8_t flags, const float health, const ItemID held_item, const glm::dvec3 &listener_pos ) {
    this->flags_map[ player_id ] = flags;
    this->eye_pos_map[ player_id ] = glm::dvec3( x, y, z );
    this->eye_height_map[ player_id ] = eye_height;
    // Health is client-authoritative: a drop means the remote player just
    // took damage — flash their avatar red + hurt sound. -1 (unknown)
    // never triggers, so a joiner's first packet can't spuriously flash.
    const float prev_health = this->health_map[ player_id ];
    this->health_map[ player_id ] = health;
    const uint32_t uid = this->uid_map[ player_id ];
    if ( uid == 0 ) {
        return;
    }
    if ( health >= 0.0f && prev_health >= 0.0f && health < prev_health ) {
        this->mobs->flash_remote_hurt( uid, listener_pos );
    }
    // The packet's (x,y,z) is the eye position; the rotation carries the
    // sender's camera angles (see mob_look_from_camera_rotation).
    float yaw = 0.0f, pitch = 0.0f;
    mob_look_from_camera_rotation( rotation, &yaw, &pitch );
    this->mobs->set_remote_pose( uid, glm::dvec3( x, y, z ), yaw, pitch, eye_height );
    this->mobs->set_remote_held_item( uid, held_item );
}

bool MultiplayerAvatars::targetable( const int player_id ) const {
    return ( this->flags_map[ player_id ] & NET_PLAYER_FLAG_TARGETABLE ) != 0;
}

void MultiplayerAvatars::fill_remote_targets( std::vector<MobRemoteTarget> &out ) const {
    for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
        if ( this->uid_map[ fd ] == 0 ) {
            continue;
        }
        out.push_back( { fd, this->eye_pos_map[ fd ], this->eye_height_map[ fd ], this->targetable( fd ) } );
    }
}

void MultiplayerAvatars::remove( const unsigned int player_id ) {
    const uint32_t uid = this->uid_map[ player_id ];
    if ( uid == 0 ) {
        return;
    }
    this->mobs->remove_remote( uid );
    this->uid_map[ player_id ] = 0;
    this->health_map[ player_id ] = -1.0f;
}

void MultiplayerAvatars::cleanup( ) {
    memset( this->uid_map, 0, sizeof( this->uid_map ) );
    memset( this->flags_map, 0, sizeof( this->flags_map ) );
    for ( float &h : this->health_map ) {
        h = -1.0f;
    }
}
