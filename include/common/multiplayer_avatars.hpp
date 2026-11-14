#pragma once

#include <stdint.h>

#include "common/constants.hpp"
#include "common/mobs.hpp"

class Vehicles;

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
    // Last health received per player (-1 unknown): a drop between updates
    // flashes that player's avatar red and plays the hurt sound.
    float health_map[ MAX_CLIENT_FDS ];
    // NetEntityId of the cart each player rides (0 = standing). Seated
    // avatars are posed at the cart's eased position each tick instead of
    // the streamed eye position.
    uint64_t riding_vehicle_map[ MAX_CLIENT_FDS ];

  public:
    void init( MobManager *mob_manager );
    void add( unsigned int player_id );
    // listener_pos fades the health-drop hurt sfx against the local camera.
    // held_item is the sender's selected hotbar item (ItemID) — the avatar
    // draws it in its right hand. riding_vehicle is the sender's ridden cart
    // net id (0 = not riding).
    void update_position( int player_id, float x, float y, float z, const glm::mat4 &rotation, float eye_height, uint8_t flags, float health, ItemID held_item, const glm::dvec3 &listener_pos, uint64_t riding_vehicle = 0 );
    bool targetable( int player_id ) const;
    // The remote player's feet position (eye_pos_map - its eye height) in
    // `feet` — false when no update for player_id has arrived yet. Used by
    // /tp <player>.
    bool position_of( int player_id, glm::dvec3 &feet ) const;
    // Fill `out` with every connected remote player's targeting info.
    void fill_remote_targets( std::vector<MobRemoteTarget> &out ) const;
    // Fill `out` with every connected remote player's feet position —
    // the pushers that shove owned empty minecarts. Seated riders are
    // skipped: they can't shove their own (or any) cart.
    void fill_remote_feet( std::vector<glm::dvec3> &out ) const;
    // Seat each riding avatar in its cart. Runs after vehicles.tick so the
    // pose tracks the cart's eased position; avatars whose cart is gone
    // (or who dismounted) fall back to the streamed standing pose.
    void apply_vehicle_seats( const Vehicles &vehicles );
    // mp_test dump seam: the cart net id a player rides (0 = standing).
    uint64_t riding_vehicle_of( int player_id ) const {
        return player_id >= 0 && player_id < MAX_CLIENT_FDS ? this->riding_vehicle_map[ player_id ] : 0;
    }
    // mp_test dump seam: the cart net id ridden by the avatar behind `uid`.
    uint64_t riding_vehicle_of_uid( uint32_t uid ) const {
        if ( uid != 0 ) {
            for ( int fd = 0; fd < MAX_CLIENT_FDS; fd++ ) {
                if ( this->uid_map[ fd ] == uid ) {
                    return this->riding_vehicle_map[ fd ];
                }
            }
        }
        return 0;
    }
    // mp_test dump seam: uid the player's avatar spawned under (0 = none).
    uint32_t uid_of( int player_id ) const {
        return player_id >= 0 && player_id < MAX_CLIENT_FDS ? this->uid_map[ player_id ] : 0;
    }
    void remove( unsigned int player_id );
    void cleanup( );
};
