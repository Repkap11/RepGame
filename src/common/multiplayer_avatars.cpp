#include "common/multiplayer_avatars.hpp"
#include "common/RepGame.hpp"

#include <string.h>

#include "common/vehicles.hpp"

void MultiplayerAvatars::init( MobManager *mob_manager ) {
    this->mobs = mob_manager;
    memset( this->uid_map, 0, sizeof( this->uid_map ) );
    memset( this->flags_map, 0, sizeof( this->flags_map ) );
    memset( this->riding_vehicle_map, 0, sizeof( this->riding_vehicle_map ) );
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

void MultiplayerAvatars::update_position( const int player_id, const float x, const float y, const float z, const glm::mat4 &rotation, const float eye_height, const uint8_t flags, const float health, const ItemID held_item, const glm::dvec3 &listener_pos, const uint64_t riding_vehicle ) {
    this->flags_map[ player_id ] = flags;
    this->eye_pos_map[ player_id ] = glm::dvec3( x, y, z );
    this->eye_height_map[ player_id ] = eye_height;
    this->riding_vehicle_map[ player_id ] = riding_vehicle;
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

bool MultiplayerAvatars::position_of( const int player_id, glm::dvec3 &feet ) const {
    if ( player_id < 0 || player_id >= MAX_CLIENT_FDS || this->uid_map[ player_id ] == 0 ||
         this->eye_height_map[ player_id ] <= 0.0f ) {
        return false;
    }
    feet = this->eye_pos_map[ player_id ];
    feet.y -= this->eye_height_map[ player_id ];
    return true;
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

void MultiplayerAvatars::fill_remote_feet( std::vector<glm::dvec3> &out ) const {
    for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
        if ( this->riding_vehicle_map[ fd ] != 0 ) {
            continue; // riders can't shove vehicles
        }
        glm::dvec3 feet;
        if ( this->position_of( fd, feet ) ) {
            out.push_back( feet );
        }
    }
}

void MultiplayerAvatars::apply_vehicle_seats( const Vehicles &vehicles ) {
    for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
        const uint32_t uid = this->uid_map[ fd ];
        if ( uid == 0 ) {
            continue;
        }
        const Vehicles::Vehicle *cart = this->riding_vehicle_map[ fd ] != 0 ? vehicles.find_by_net_id( this->riding_vehicle_map[ fd ] ) : nullptr;
        if ( cart != nullptr ) {
            this->mobs->set_remote_seated( uid, Vehicles::seat_pos( *cart ), cart->yaw );
        } else {
            this->mobs->set_remote_seated( uid, glm::dvec3( 0.0 ), 0.0f, false );
        }
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
    this->riding_vehicle_map[ player_id ] = 0;
}

void MultiplayerAvatars::cleanup( ) {
    // Zeroing uid_map without remove() would orphan the spawned remote mobs
    // (and their held-item instances) in the MobManager forever.
    for ( unsigned int player_id = 0; player_id < MAX_CLIENT_FDS; player_id++ ) {
        this->remove( player_id );
    }
    memset( this->flags_map, 0, sizeof( this->flags_map ) );
    for ( float &h : this->health_map ) {
        h = -1.0f;
    }
}
