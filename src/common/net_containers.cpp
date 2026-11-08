// Client container sync — see net_containers.hpp. Inbound frames only
// update World-owned container state (+ the pending-loot flag); the open
// UI panel repaints itself each tick through repgame_*_sync_icons, so
// nothing here touches ui state.

#include "common/net_containers.hpp"

#include "common/RepGame.hpp"
#include "common/multiplayer.hpp"
#include "common/world.hpp"

void NetContainers::open( NetContainerKind kind, const glm::ivec3 &pos ) {
    const NetContainerPosPayload p{ kind, pos.x, pos.y, pos.z };
    this->mp->send( NetMsgType::CONTAINER_OPEN, p );
}

void NetContainers::close( NetContainerKind kind, const glm::ivec3 &pos ) {
    const NetContainerPosPayload p{ kind, pos.x, pos.y, pos.z };
    this->mp->send( NetMsgType::CONTAINER_CLOSE, p );
}

void NetContainers::set_slot( NetContainerKind kind, const glm::ivec3 &pos, uint32_t slot_index, const InventorySlot &value ) {
    const NetContainerSetSlotPayload p{ kind, pos.x, pos.y, pos.z, slot_index, value, 0 };
    this->mp->send( NetMsgType::CONTAINER_SET_SLOT, p );
}

void NetContainers::reset( ) {
    for ( auto &rev : this->revisions ) {
        rev.clear( );
    }
}

uint32_t NetContainers::last_revision( NetContainerKind kind, const glm::ivec3 &pos ) const {
    const uint8_t k = static_cast<uint8_t>( kind );
    if ( k >= NET_CONTAINER_KIND_COUNT ) {
        return 0;
    }
    const auto it = this->revisions[ k ].find( pos );
    return it == this->revisions[ k ].end( ) ? 0 : it->second;
}

bool NetContainers::handle( RepGameState &state, NetMsgType type, int32_t player_id, PacketReader &r ) {
    World &world = state.world;
    ( void )player_id; // container frames are server-originated; id unused
    switch ( type ) {
        case NetMsgType::CONTAINER_CONTENTS: {
            NetContainerContentsPayload p;
            if ( !net_deserialize_container_contents( r, p ) ) {
                pr_debug( "Malformed CONTAINER_CONTENTS, skipping" );
                return true;
            }
            const NetContainerKindDef *def = net_container_kind_def( static_cast<uint8_t>( p.kind ) );
            const glm::ivec3 pos( p.x, p.y, p.z );
            this->revisions[ static_cast<uint8_t>( p.kind ) ][ pos ] = p.revision;
            // Two roles: the answer to CONTAINER_OPEN for a live container,
            // or the loot snapshot for one we broke (pending_container_loot).
            // The pending flag is consumed either way so a stale entry can't
            // loot a future container placed at the same position.
            if ( state.pending_container_loot.erase( pos ) ) {
                if ( !def->block_is( world.get_loaded_block( pos ).id ) ) {
                    repgame_loot_container( state, p.slots.data( ), static_cast<int>( p.slots.size( ) ) );
                }
            } else if ( def->block_is( world.get_loaded_block( pos ).id ) ) {
                ContainerContents c;
                c.slots = std::move( p.slots );
                c.aux = std::move( p.aux );
                world.set_container_contents( p.kind, pos, c );
            }
            return true;
        }
        case NetMsgType::CONTAINER_SET_SLOT: {
            NetContainerSetSlotPayload p;
            if ( !net_deserialize_container_set_slot( r, p ) ) {
                pr_debug( "Malformed CONTAINER_SET_SLOT, skipping" );
                return true;
            }
            const glm::ivec3 pos( p.x, p.y, p.z );
            this->revisions[ static_cast<uint8_t>( p.kind ) ][ pos ] = p.revision;
            // Authoritative write from another viewer (or the correction for
            // one of ours the server normalized). World applies it; the open
            // panel repaints itself from World each tick.
            world.set_container_slot( p.kind, pos, static_cast<int>( p.slot_index ), p.slot_value );
            return true;
        }
        default:
            return false;
    }
}
