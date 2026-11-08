#include "common/harness/debug_commands.hpp"

#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "common/RepGame.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/item.hpp"
#include "common/multiplayer.hpp"
#include "common/net_entities.hpp"

// Same name lookup item_entities uses for REPGAME_ITEM_TEST: every defined
// block name plus the pure item names, case-insensitive.
ItemID dbgcmd::item_from_name( const char *name ) {
    for ( unsigned int id = 0; id < static_cast<unsigned int>( LAST_BLOCK_ID ); id++ ) {
        if ( !block_id_defined( id ) ) {
            continue;
        }
        const char *n = block_definition_get_name( static_cast<BlockID>( id ) );
        if ( n != nullptr && strcasecmp( n, name ) == 0 ) {
            return id;
        }
    }
    for ( ItemID id = ITEM_ID_BASE; id < ITEM_ID_END; id++ ) {
        const char *n = item_name( id );
        if ( n != nullptr && strcasecmp( n, name ) == 0 ) {
            return id;
        }
    }
    return LAST_BLOCK_ID;
}

BlockID dbgcmd::block_from_name( const char *name ) {
    char *end = nullptr;
    const long id = strtol( name, &end, 10 );
    if ( end != name && *end == '\0' && id >= 0 && id < static_cast<long>( LAST_BLOCK_ID ) &&
         block_id_defined( static_cast<unsigned int>( id ) ) ) {
        return static_cast<BlockID>( id );
    }
    return static_cast<BlockID>( item_from_name( name ) );
}

MobType dbgcmd::mob_from_name( const char *name ) {
    for ( int t = 0; t < MOB_TYPE_COUNT; t++ ) {
        if ( mob_types[ t ].name != nullptr && strcasecmp( mob_types[ t ].name, name ) == 0 ) {
            return static_cast<MobType>( t );
        }
    }
    return MOB_TYPE_COUNT;
}

bool dbgcmd::loader_idle( const ChunkLoader &loader ) {
    LoaderDebugStats s;
    loader.debug_loader_stats( &s );
    return s.chunks_loading == 0 && s.terrain_queued == 0 && s.terrain_results == 0 && s.light_jobs == 0 && s.light_pending == 0 &&
           s.light_dirty == 0 && s.light_bfs_active == 0 && s.light_seeds == 0 && s.light_stale_count == 0;
}

void dbgcmd::goto_( RepGameState &state, const glm::dvec3 &pos ) {
    RepGame::mp_test_goto( state, pos );
}

bool dbgcmd::drop( RepGameState &state, const char *item, int count, const glm::dvec3 &pos, const glm::dvec3 &vel, std::string *err ) {
    const ItemID id = item_from_name( item );
    if ( id == LAST_BLOCK_ID ) {
        *err = std::string( "unknown item " ) + item;
        return false;
    }
    state.world.items.spawn( id, count > 0 ? count : 1, pos, vel, 10 );
    return true;
}

bool dbgcmd::mob( RepGameState &state, const char *type, const glm::dvec3 &pos, std::string *err ) {
    const MobType t = mob_from_name( type );
    if ( t == MOB_TYPE_COUNT || !state.world.mobs.spawn( t, pos, 0.0f, 1, false ) ) {
        *err = std::string( "cannot spawn mob " ) + type;
        return false;
    }
    return true;
}

// Player melee hit on the mob carrying `net_id` — same shape as the
// left-click path: mirrors get local feedback + ENTITY_ACTION, owned take
// real damage.
bool dbgcmd::hit( RepGameState &state, const unsigned long long net_id, std::string *err ) {
    MobManager &mobs = state.world.mobs;
    for ( size_t i = 0; i < mobs.count( ); i++ ) {
        if ( mobs.net_id_at( static_cast<int>( i ) ) != net_id ) {
            continue;
        }
        if ( mobs.net_mirror_at( static_cast<int>( i ) ) ) {
            mobs.mirror_hit_feedback( static_cast<int>( i ), state.camera.pos );
            NetEntityActionPayload act{};
            act.id = net_id;
            act.action = NET_ACTION_HIT;
            act.damage = state.game_mode == GameMode_Survival ? 4.0f : 1000.0f;
            act.ax = static_cast<float>( state.camera.pos.x );
            act.ay = static_cast<float>( state.camera.pos.y );
            act.az = static_cast<float>( state.camera.pos.z );
            act.attacker_survival = state.game_mode == GameMode_Survival ? 1 : 0;
            state.multiplayer.entities.send_entity_action( act );
        } else {
            const bool killed = mobs.hurt( static_cast<int>( i ), state.game_mode == GameMode_Survival ? 4.0f : 1000.0f, state.camera.pos );
            if ( killed && state.game_mode == GameMode_Survival ) {
                RepGame::spawn_mob_drops( state, mobs.def_at( static_cast<int>( i ) ), mobs.pos_at( static_cast<int>( i ) ) );
            }
        }
        return true;
    }
    *err = "hit: no mob with that net_id";
    return false;
}

// Player right-click feed on the mob nearest `pos` — same shape as the
// click path: owned mobs feed directly, mirrors get ENTITY_ACTION /
// NET_ACTION_FEED for the owner to validate. `item` names a breed food
// (empty picks the type's first).
bool dbgcmd::feed( RepGameState &state, const glm::dvec3 &pos, const char *item, std::string *err ) {
    MobManager &mobs = state.world.mobs;
    int best = -1;
    double best_d = 1e9;
    for ( size_t i = 0; i < mobs.count( ); i++ ) {
        const double d = glm::length( mobs.pos_at( static_cast<int>( i ) ) - pos );
        if ( d < best_d ) {
            best_d = d;
            best = static_cast<int>( i );
        }
    }
    if ( best < 0 ) {
        *err = "feed: no mobs";
        return false;
    }
    ItemID food = LAST_BLOCK_ID;
    if ( item != nullptr && item[ 0 ] != '\0' ) {
        food = item_from_name( item );
        if ( food == LAST_BLOCK_ID ) {
            *err = std::string( "feed: unknown item " ) + item;
            return false;
        }
    } else {
        const MobTypeDef &def = mobs.def_at( best );
        food = def.breed_items[ 0 ];
    }
    if ( food == LAST_BLOCK_ID || !mobs.feedable( best, food ) ) {
        *err = "feed: mob won't eat that";
        return false;
    }
    if ( mobs.net_mirror_at( best ) ) {
        NetEntityActionPayload act{};
        act.id = mobs.net_id_at( best );
        act.action = NET_ACTION_FEED;
        act.damage = 0.0f;
        act.ax = static_cast<float>( state.camera.pos.x );
        act.ay = static_cast<float>( state.camera.pos.y );
        act.az = static_cast<float>( state.camera.pos.z );
        act.attacker_survival = state.game_mode == GameMode_Survival ? 1 : 0;
        act.item = static_cast<uint32_t>( food );
        state.multiplayer.entities.send_entity_action( act );
    } else {
        mobs.feed( best, food );
    }
    return true;
}

// Player right-click shear on the shearable mob nearest `pos` — same shape
// as the click path: owned mobs shear directly (+ shear_drop spawn),
// mirrors get ENTITY_ACTION / NET_ACTION_SHEAR for the owner to validate.
bool dbgcmd::shear( RepGameState &state, const glm::dvec3 &pos, std::string *err ) {
    MobManager &mobs = state.world.mobs;
    int best = -1;
    double best_d = 1e9;
    for ( size_t i = 0; i < mobs.count( ); i++ ) {
        const double d = glm::length( mobs.pos_at( static_cast<int>( i ) ) - pos );
        if ( d < best_d ) {
            best_d = d;
            best = static_cast<int>( i );
        }
    }
    if ( best < 0 ) {
        *err = "shear: no mobs";
        return false;
    }
    if ( !mobs.shearable( best, SHEARS ) ) {
        *err = "shear: mob isn't shearable";
        return false;
    }
    if ( mobs.net_mirror_at( best ) ) {
        NetEntityActionPayload act{};
        act.id = mobs.net_id_at( best );
        act.action = NET_ACTION_SHEAR;
        act.damage = 0.0f;
        act.ax = static_cast<float>( state.camera.pos.x );
        act.ay = static_cast<float>( state.camera.pos.y );
        act.az = static_cast<float>( state.camera.pos.z );
        act.attacker_survival = state.game_mode == GameMode_Survival ? 1 : 0;
        act.item = static_cast<uint32_t>( SHEARS );
        state.multiplayer.entities.send_entity_action( act );
    } else if ( mobs.shear( best, SHEARS, state.camera.pos ) ) {
        RepGame::spawn_mob_shear_drop( state, mobs.def_at( best ), mobs.pos_at( best ) );
    }
    return true;
}

bool dbgcmd::fall( RepGameState &state, const char *item, const glm::ivec3 &pos, std::string *err ) {
    const ItemID id = item_from_name( item );
    if ( id == LAST_BLOCK_ID || !block_id_defined( id ) ) {
        *err = std::string( "unknown block " ) + item;
        return false;
    }
    const BlockState bs{ static_cast<BlockID>( id ), 0, 0, static_cast<BlockID>( id ), 0 };
    state.world.bodies.spawn_falling( pos, bs, glm::dvec3( 0.0 ) );
    return true;
}

bool dbgcmd::tnt( RepGameState &state, const glm::vec3 &center, std::string *err ) {
    // The normal launch path: body + TntExplodeEvent at tick + fuse.
    (void)err;
    tnt_launch( state.blockUpdateQueue, state, center, glm::vec3( 0.0f, 4.0f, 0.0f ), TNT_FUSE_TICKS, false );
    return true;
}

bool dbgcmd::setblock( RepGameState &state, const glm::ivec3 &pos, const char *block_name, std::string *err ) {
    const ItemID id = item_from_name( block_name );
    if ( id == LAST_BLOCK_ID ) {
        *err = std::string( "unknown block " ) + block_name;
        return false;
    }
    const BlockState bs{ static_cast<BlockID>( id ), 0, 0, static_cast<BlockID>( id ), 0 };
    // setblock plays the player role: removing a container marks the loot
    // marker the mouse-break path sets, so the server's CONTAINER_CONTENTS
    // snapshot lands in the breaker's inventory. Only AIR writes qualify —
    // a non-AIR write over a container is rejected by performAction's
    // replaceable-cell rule and mustn't leave a stale marker.
    if ( bs.id == AIR && state.multiplayer.is_active( ) &&
         net_container_kind_of_block( state.world.get_loaded_block( pos ).id ) != nullptr ) {
        state.pending_container_loot.insert( pos );
    }
    state.blockUpdateQueue.addBlockUpdate( std::make_shared<PlayerBlockPlacedEvent>( state.tick_number, pos, bs, false ) );
    return true;
}

bool dbgcmd::fill( RepGameState &state, const glm::ivec3 &a, const glm::ivec3 &b, const char *block_name, std::string *err ) {
    const ItemID id = item_from_name( block_name );
    if ( id == LAST_BLOCK_ID ) {
        *err = std::string( "unknown block " ) + block_name;
        return false;
    }
    const glm::ivec3 lo( std::min( a.x, b.x ), std::min( a.y, b.y ), std::min( a.z, b.z ) );
    const glm::ivec3 hi( std::max( a.x, b.x ), std::max( a.y, b.y ), std::max( a.z, b.z ) );
    const glm::ivec3 size = hi - lo + glm::ivec3( 1 );
    const long long cells = static_cast<long long>( size.x ) * size.y * size.z;
    if ( cells > 65536 ) {
        *err = "fill: volume exceeds 65536 cells";
        return false;
    }
    const BlockState bs{ static_cast<BlockID>( id ), 0, 0, static_cast<BlockID>( id ), 0 };
    for ( int x = lo.x; x <= hi.x; x++ ) {
        for ( int y = lo.y; y <= hi.y; y++ ) {
            for ( int z = lo.z; z <= hi.z; z++ ) {
                state.blockUpdateQueue.addBlockUpdate(
                    std::make_shared<PlayerBlockPlacedEvent>( state.tick_number, glm::ivec3( x, y, z ), bs, false ) );
            }
        }
    }
    return true;
}

const NetContainerKindDef *dbgcmd::container_from_name( const char *name ) {
    for ( uint8_t k = 0; k < NET_CONTAINER_KIND_COUNT; k++ ) {
        const NetContainerKindDef *def = net_container_kind_def( k );
        if ( def != nullptr && strcasecmp( def->name, name ) == 0 ) {
            return def;
        }
    }
    return nullptr;
}

bool dbgcmd::copen( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, std::string *err ) {
    if ( def == nullptr ) {
        *err = "copen: unknown container kind";
        return false;
    }
    state.multiplayer.containers.open( def->kind, pos );
    return true;
}

bool dbgcmd::cclose( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, std::string *err ) {
    if ( def == nullptr ) {
        *err = "cclose: unknown container kind";
        return false;
    }
    state.multiplayer.containers.close( def->kind, pos );
    return true;
}

bool dbgcmd::cset( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, int slot, const char *item, int count,
                   std::string *err ) {
    if ( def == nullptr ) {
        *err = "cset: unknown container kind";
        return false;
    }
    if ( slot < 0 || slot >= def->slots ) {
        *err = "cset: slot out of range";
        return false;
    }
    const ItemID id = item_from_name( item );
    if ( id == LAST_BLOCK_ID && count != 0 ) {
        *err = std::string( "cset: unknown item " ) + item;
        return false;
    }
    const InventorySlot value{ id, count };
    state.world.set_container_slot( def->kind, pos, slot, value );
    state.multiplayer.containers.set_slot( def->kind, pos, static_cast<uint32_t>( slot ), value );
    return true;
}

bool dbgcmd::cprobe( RepGameState &state, const NetContainerKindDef *def, const glm::ivec3 &pos, ContainerContents *out,
                     bool *is_container, std::string *err ) {
    if ( def == nullptr ) {
        *err = "cprobe: unknown container kind";
        return false;
    }
    const BlockState bs = state.world.get_loaded_block( pos );
    if ( bs.id == LAST_BLOCK_ID ) {
        *err = "cprobe: chunk not loaded";
        return false;
    }
    *is_container = def->block_is( bs.id );
    out->slots.assign( def->slots, InventorySlot{ LAST_BLOCK_ID, 0 } );
    out->aux.assign( def->aux, 0 );
    state.world.get_container_contents( def->kind, pos, *out );
    return true;
}

bool dbgcmd::mode( RepGameState &state, const char *name, bool *force_survival, std::string *err ) {
    if ( strcasecmp( name, "survival" ) != 0 && strcasecmp( name, "creative" ) != 0 ) {
        *err = std::string( "mode: want survival|creative, got " ) + name;
        return false;
    }
    const bool survival = strcasecmp( name, "survival" ) == 0;
    if ( force_survival != nullptr ) {
        *force_survival = survival;
    }
    state.game_mode = survival ? GameMode_Survival : GameMode_Creative;
    return true;
}

bool dbgcmd::peaceful( RepGameState &state, const char *name, std::string *err ) {
    if ( strcasecmp( name, "on" ) != 0 && strcasecmp( name, "off" ) != 0 ) {
        *err = std::string( "peaceful: want on|off, got " ) + name;
        return false;
    }
    state.peaceful = strcasecmp( name, "on" ) == 0;
    return true;
}

void dbgcmd::set_time( RepGameState &state, const long ticks ) {
    state.world_time = ticks;
    state.multiplayer.send_world_time( state.world_time );
}
