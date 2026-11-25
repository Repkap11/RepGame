// RepGame member functions: player vitals, damage, eating, sleeping, and respawn.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/dimension_profile.hpp"
#include "common/fluid.hpp"
#include "common/map_gen.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/harness/debug_commands.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/TntExplodeEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/block_update_events/schedule.hpp"
#include "common/game/game_internal.hpp"
#include "common/break_drops.hpp"
#include "common/rail.hpp"
#include "common/repeater.hpp"
#include "common/tree_grow.hpp"
#include "common/vehicle_physics.hpp" // VEHICLE_BOAT_DRAFT


// Hotbar first (stacking onto matching slots), survival inventory for the
// overflow — shared by block-break pickups and mob drops.
// Applies `hp` damage (half-hearts) to the local player — survival only
// (creative is invulnerable), gated by the shared post-hit i-frame window
// that also paces the periodic sources (lava, fire, drowning) to ~2/s.
// Reaching 0 raises the death screen via dead + menu_open.
bool RepGame::player_damage( RepGameState &state, const float hp ) {
    if ( hp <= 0.0f || state.game_mode != GameMode_Survival || state.vitals.dead || state.vitals.hurt_cooldown > 0 ) {
        return false;
    }
    state.vitals.health -= hp;
    state.vitals.hurt_cooldown = PLAYER_HURT_COOLDOWN_TICKS;
    if ( state.vitals.health <= 0.0f ) {
        state.vitals.health = 0.0f;
        state.vitals.dead = true;
        state.ui.pause.set_dead( state, true );
        state.input.menu_open = true;
    }
    return true;
}

void RepGame::player_heal( RepGameState &state, const float hp ) {
    state.vitals.health = fminf( state.vitals.health + hp, PLAYER_MAX_HEALTH );
}

// Mob melee hits: a small knockback hop away from the attacker plus the hurt
// sound, then survival health damage through the shared funnel.
void RepGame::player_hurt( RepGameState &state, const glm::dvec3 &from, float damage ) {
    if ( state.vitals.dead ) {
        return;
    }
    glm::dvec3 away = state.camera.pos - from;
    away.y = 0.0;
    const double len = glm::length( away );
    if ( len > 1e-6 ) {
        away /= len;
        state.camera.horizontal_vel += glm::dvec2( away.x, away.z ) * 0.06;
        if ( state.camera.standing_on_solid ) {
            state.camera.y_speed = 0.6 * JUMP_STRENGTH;
        }
    }
    audio_play_sfx( "player_hurt" );
    player_damage( state, damage );
}

// Explosion blast on the local player — damage falls off linearly with
// distance and the shove scales with it. Both local blasts
// (TntExplodeEvent) and remote ones (inbound EXPLOSION) land here; health
// is client-authoritative, so every client damages only itself.
void repgame_explosion_affect_player( RepGameState &state, const glm::dvec3 &center, const float radius ) {
    const glm::dvec3 delta = state.camera.pos - center;
    const double dist = glm::length( delta );
    if ( dist >= radius || dist < 1e-6 || state.vitals.dead ) {
        return;
    }
    const double scale = 1.0 - dist / radius;
    const glm::dvec3 away = delta / dist;
    state.camera.horizontal_vel += glm::dvec2( away.x, away.z ) * scale * 0.3;
    state.camera.y_speed += scale * JUMP_STRENGTH * 1.5;
    if ( RepGame::player_damage( state, static_cast<float>( scale ) * PLAYER_EXPLOSION_MAX_DAMAGE ) ) {
        audio_play_sfx( "player_hurt" );
    }
}

// Right-click eat: heals instantly like beta MC — one item from the held
// stack per call, only while hurt (a full-health click wastes nothing).
bool repgame_eat_food( RepGameState &state ) {
    const ItemID held = state.hotbar.getSelectedItem( );
    const int heal = item_food_heal( held );
    if ( heal <= 0 || state.vitals.health >= PLAYER_MAX_HEALTH ) {
        return false;
    }
    RepGame::player_heal( state, static_cast<float>( heal ) );
    if ( state.game_mode == GameMode_Survival ) {
        state.hotbar.consumeSelected( 1 );
        state.ui.overlay.set_holding_item( state.hotbar.getSelectedItem( ) );
    }
    audio_play_sfx( "eat" );
    // The burp caps a meal, not every bite: only when this item tops the
    // health bar off (beta instant-eat has no finish-eating moment).
    if ( state.vitals.health >= PLAYER_MAX_HEALTH ) {
        audio_play_sfx( "burp" );
    }
    state.vr.haptic_pending = true;
    return true;
}

// Aim the player at the respawn column — the recorded bed when
// spawn_point_set, else the world-spawn column — and let spawn_pending
// snap them onto its ground once it loads. Vitals are untouched: shared
// by the death-screen respawn and the End -> overworld return.
void repgame_goto_respawn_point( RepGameState &state ) {
    state.vitals.fall_dist = 0.0;
    CameraState &cam = state.camera;
    cam.horizontal_vel = glm::dvec2( 0.0 );
    cam.y_speed = 0.0;
    if ( state.spawn_point_set ) {
        cam.pos = glm::dvec3( state.spawn_point.x + 0.5, state.spawn_point.y + 2.0, state.spawn_point.z + 0.5 );
    } else {
        cam.pos = glm::dvec3( 0.5, ceil( MapGen::maxTerrainHeight( DIM_OVERWORLD ) ) + 2.0, 0.5 );
    }
    state.spawn_pending = true;
    state.spawn_world_pending = false; // a respawn always aims at the bed/spawn column — drop any /spawn pin
    cam.snapshot_prev( );
}

// Death-screen exit: stand the player back on the spawn column — the
// recorded bed when spawn_point_set, else the world-spawn column — with
// full vitals, inventory kept. spawn_pending reuses the same topmost-
// colliding-block snap as a fresh spawn once the column loads. A free
// function so rep_tests and the mp-test ctl can drive it.
void repgame_respawn( RepGameState &state ) {
    PlayerVitals &v = state.vitals;
    v.health = PLAYER_MAX_HEALTH;
    v.air_ticks = PLAYER_AIR_MAX_TICKS;
    v.hurt_cooldown = 0;
    v.regen_accum = 0;
    v.dead = false;
    repgame_goto_respawn_point( state );
    state.ui.pause.set_dead( state, false );
    state.input.menu_open = false;
}

void RepGame::respawn( ) {
    repgame_respawn( globalGameState );
}

// Right-clicking a bed: always records it as the respawn point (the column
// repgame_respawn/resolve_pending_spawn aim at); from dusk on
// (SLEEP_EARLIEST_TICKS — when the daylight first starts dimming)
// additionally sleeps the world clock to the next dawn. A non-bed or
// unloaded cell is refused outright (the mp-test/script `sleep x y z`
// command points the function at arbitrary cells). Sleeping is
// refused while a hostile mob is within SLEEP_MONSTER_RADIUS (MC's
// "monsters nearby" rule) — the spawn point is still set either way. In
// multiplayer the new time goes out as WORLD_TIME like the PgUp/PgDn and
// dbgcmd::set_time writes. A free function so rep_tests and the mp-test
// ctl can drive it.
bool repgame_try_sleep( RepGameState &state, const glm::ivec3 &bed_pos ) {
    const BlockState bed_bs = state.world.get_loaded_block( bed_pos );
    if ( bed_bs.id == LAST_BLOCK_ID || !block_definition_get_definition( bed_bs.id )->is_bed ) {
        state.chat.push( CHAT_KIND_ERROR, "", "", "Cannot sleep: no bed there", state.tick_number );
        return false;
    }
    // Beds explode in dimensions whose profile forbids sleeping (the nether):
    // remove the bed (the partner half pops through the neighbour kill) and
    // drop a short-fuse TNT where it stood.
    if ( dimension_profile( dimension_of_pos( bed_pos ) )->beds_explode ) {
        schedule_block_update<PlayerBlockPlacedEvent>( state, 0, bed_pos, BLOCK_STATE_AIR, false );
        tnt_launch( state, glm::vec3( bed_pos ) + glm::vec3( 0.5f ), glm::vec3( 0.0f ), UPS_RATE / 2, false );
        return false;
    }
    state.spawn_point = bed_pos;
    state.spawn_point_set = true;
    const long t = state.world_time % DAY_LENGTH_TICKS;
    if ( t < SLEEP_EARLIEST_TICKS ) {
        state.chat.push( CHAT_KIND_INFO, "", "", "Respawn point set (can only sleep at dusk or night)", state.tick_number );
        return false;
    }
    if ( state.world.mobs.hostiles_within( state.camera.pos, SLEEP_MONSTER_RADIUS ) ) {
        state.chat.push( CHAT_KIND_INFO, "", "", "Respawn point set (cannot sleep: monsters nearby)", state.tick_number );
        return false;
    }
    state.world_time = ( state.world_time / DAY_LENGTH_TICKS + 1 ) * DAY_LENGTH_TICKS + SLEEP_WAKE_OFFSET;
    state.multiplayer.send_world_time( state.world_time );
    state.chat.push( CHAT_KIND_INFO, "", "", "Respawn point set (sleeping until dawn)", state.tick_number );
    return true;
}

// Per-tick vitals upkeep — runs right after movement each tick: hurt
// i-frames count down, a submerged head drains breath then drowns,
// lava/fire contact burns, and the peaceful "Mobs" toggle slowly
// regenerates health (MC's peaceful difficulty). A free function so
// rep_tests can drive it.
void repgame_tick_vitals( RepGameState &state ) {
    PlayerVitals &v = state.vitals;
    if ( v.hurt_cooldown > 0 ) {
        v.hurt_cooldown--;
    }
    if ( v.dead ) {
        v.air_ticks = PLAYER_AIR_MAX_TICKS;
        // The death screen may not be dismissed by Esc/E — it can only be
        // left through Respawn (or Exit).
        state.input.menu_open = true;
        return;
    }
    const glm::dvec3 &eye = state.camera.pos;
    const double feet_y = eye.y - state.camera.eye_height;
    const BlockState eye_block = state.world.get_loaded_block( glm::ivec3( glm::round( eye - glm::dvec3( 0.5 ) ) ) );
    const glm::ivec3 feet_cell( glm::round( glm::dvec3( eye.x, feet_y + 0.05, eye.z ) - glm::dvec3( 0.5 ) ) );
    const BlockState feet_block = state.world.get_loaded_block( feet_cell );
    const glm::dvec3 body_center( eye.x, feet_y + PLAYER_HEIGHT / 2.0, eye.z );
    const BlockState body_block = state.world.get_loaded_block( glm::ivec3( glm::round( body_center - glm::dvec3( 0.5 ) ) ) );
    // Breath drains while the head is under water (the render path's
    // headInWater test uses the same eye cell) and refills at the surface.
    if ( fluid_in_cell( eye_block ) == WATER ) {
        if ( v.air_ticks > 0 ) {
            v.air_ticks--;
        } else if ( RepGame::player_damage( state, PLAYER_DROWN_DAMAGE ) ) {
            audio_play_sfx( "drown" );
        }
    } else {
        // Surfacing gasp: the breath meter refills instantly, so the edge
        // is air_ticks below max on the first dry tick. Gain scales with
        // the fraction of the meter actually refilled.
        if ( v.air_ticks < PLAYER_AIR_MAX_TICKS ) {
            audio_play_sfx_gain( "breath", 0.5f * (float)( PLAYER_AIR_MAX_TICKS - v.air_ticks ) / (float)PLAYER_AIR_MAX_TICKS );
        }
        v.air_ticks = PLAYER_AIR_MAX_TICKS;
    }
    // Lava burns fast anywhere it touches the body; open fire burns slowly.
    if ( fluid_in_cell( eye_block ) == LAVA || fluid_in_cell( body_block ) == LAVA || fluid_in_cell( feet_block ) == LAVA ) {
        if ( RepGame::player_damage( state, PLAYER_LAVA_DAMAGE ) ) {
            audio_play_sfx( "fire_hurt" );
        }
    } else if ( feet_block.id == FIRE || body_block.id == FIRE || feet_block.id == SOUL_FIRE || body_block.id == SOUL_FIRE ) {
        if ( RepGame::player_damage( state, PLAYER_FIRE_DAMAGE ) ) {
            audio_play_sfx( "fire_hurt" );
        }
    } else {
        // Standing on magma burns like open fire — only when grounded (a
        // jump over it is safe) and not sneaking, flying or no-clipping
        // (MC's exemptions).
        const BlockState below_feet = state.world.get_loaded_block( feet_cell - glm::ivec3( 0, 1, 0 ) );
        if ( below_feet.id == MAGMA && state.camera.standing_on_solid && !state.input.movement.sneakPressed &&
             !state.input.player_flying && !state.input.no_clip ) {
            if ( RepGame::player_damage( state, PLAYER_MAGMA_DAMAGE ) ) {
                audio_play_sfx( "fire_hurt" );
            }
        }
    }
    if ( state.peaceful && state.game_mode == GameMode_Survival && v.health < PLAYER_MAX_HEALTH ) {
        if ( ++v.regen_accum >= PLAYER_PEACEFUL_REGEN_TICKS ) {
            v.regen_accum = 0;
            RepGame::player_heal( state, 1.0f );
        }
    } else {
        v.regen_accum = 0;
    }
}

void RepGame::tick_vitals( ) {
    repgame_tick_vitals( globalGameState );
}
