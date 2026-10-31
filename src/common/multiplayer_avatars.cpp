#include "common/multiplayer_avatars.hpp"
#include "common/RepGame.hpp"

#include <string.h>

void MultiplayerAvatars::init( MobManager *mob_manager ) {
    this->mobs = mob_manager;
    memset( this->uid_map, 0, sizeof( this->uid_map ) );
}

void MultiplayerAvatars::add( const unsigned int player_id ) {
    // A second add for a live id would orphan the old mob (rendered forever
    // at its last position) — remove it first.
    this->remove( player_id );
    this->uid_map[ player_id ] = this->mobs->spawn_remote( MOB_PLAYER );
}

void MultiplayerAvatars::update_position( const int player_id, const float x, const float y, const float z, const glm::mat4 &rotation ) {
    const uint32_t uid = this->uid_map[ player_id ];
    if ( uid == 0 ) {
        return;
    }
    // The packet's (x,y,z) is the eye position; the rotation carries the
    // sender's camera angles (see mob_look_from_camera_rotation).
    float yaw = 0.0f, pitch = 0.0f;
    mob_look_from_camera_rotation( rotation, &yaw, &pitch );
    this->mobs->set_remote_pose( uid, glm::dvec3( x, y, z ), yaw, pitch );
}

void MultiplayerAvatars::remove( const unsigned int player_id ) {
    const uint32_t uid = this->uid_map[ player_id ];
    if ( uid == 0 ) {
        return;
    }
    this->mobs->remove_remote( uid );
    this->uid_map[ player_id ] = 0;
}

void MultiplayerAvatars::cleanup( ) {
    memset( this->uid_map, 0, sizeof( this->uid_map ) );
}
