// RepGame member functions: block interaction, camera/movement input, mouse events.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
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
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/BlockNextToChangeEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/game/game_internal.hpp"
#include "common/break_drops.hpp"
#include "common/tree_grow.hpp"

// Left-click damage to a mob in survival (creative kills in one hit).
#define HIT_DAMAGE 4.0f

BlockID RepGame::change_block( const int place, BlockState blockState ) {

    glm::ivec3 block;
    if ( place ) {
        // Placing a leaf item writes the player-placed (persistent) variant —
        // only generator-grown leaves decay.
        blockState.id = block_placed_leaf_id( blockState.id );
        blockState.display_id = blockState.id;
        block = globalGameState.block_selection.pos_create;
        const Block *placed_def = block_definition_get_definition( blockState.id );
        if ( placed_def->collides_with_player ) {
            // If the block collides with the player, make sure it's not being placed where it would collide
            if ( Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos, block, //
                                                       globalGameState.camera.player_height, globalGameState.camera.eye_height ) ) {
                return LAST_BLOCK_ID;
            }
            // A door lower also occupies the cell above.
            if ( placed_def->is_door && !placed_def->is_door_upper &&
                 Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos, block + glm::ivec3( 0, 1, 0 ), //
                                                       globalGameState.camera.player_height, globalGameState.camera.eye_height ) ) {
                return LAST_BLOCK_ID;
            }
        }
    } else {
        block = globalGameState.block_selection.pos_destroy;
        // Mining a piston head breaks the piston it belongs to — the head
        // cell is only the base's collision face: retarget the removal at
        // the base so the drop and debris land on the piston, and the head
        // pops off through the support check. An orphaned head breaks in
        // place.
        const BlockState target_state = globalGameState.world.get_loaded_block( block );
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_piston_head ) {
            block = piston_head_base( globalGameState.world, block, target_state );
        }
    }
    if ( place && !player_placement_valid( globalGameState.world, block, blockState ) ) {
        return LAST_BLOCK_ID;
    }
    BlockID previous_block_id = globalGameState.world.get_loaded_block( block ).id;
    // globalGameState.world.set_loaded_block( block, blockState );
    globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number, block, blockState, false );
    return previous_block_id;
}

unsigned char RepGame::getPlacedRotation( const BlockID blockID ) const {
    unsigned char rotation = BLOCK_ROTATE_0;
    const Block *block = block_definition_get_definition( blockID );
    if ( block->rotate_on_placement ) {
        // Wall-mounted blocks (ladders, buttons) anchor to the face the ray
        // actually hit, not the yaw bucket, so an oblique click still lands
        // on the clicked face; floor/ceiling clicks fall back to yaw.
        const glm::ivec3 mount_dir = globalGameState.block_selection.pos_create - globalGameState.block_selection.pos_destroy;
        const int mounted_rotation = block_mount_rotation( block, mount_dir );
        if ( mounted_rotation >= 0 ) {
            rotation = static_cast<unsigned char>( mounted_rotation );
        } else if ( globalGameState.camera.angle_H < 45 ) {
            rotation = BLOCK_ROTATE_0;
        } else if ( globalGameState.camera.angle_H < 135 ) {
            rotation = BLOCK_ROTATE_90;
        } else if ( globalGameState.camera.angle_H < 225 ) {
            rotation = BLOCK_ROTATE_180;
        } else if ( globalGameState.camera.angle_H < 315 ) {
            rotation = BLOCK_ROTATE_270;
        } else {
            rotation = BLOCK_ROTATE_0;
        }
    }
    // Stairs flip upside-down and slabs ride the top half (rotation bit 2)
    // by MC's rule: clicking the bottom face of a block, or the upper half
    // of a side face, places the top-half variant.
    if ( block->is_stairs || block->is_slab ) {
        const BlockSelection &sel = globalGameState.block_selection;
        if ( sel.face == FACE_BOTTOM || ( sel.face != FACE_TOP && sel.hit_y_in_cell > 0.5 ) ) {
            rotation = static_cast<unsigned char>( rotation | BLOCK_ROTATE_STAIRS_TOP );
        }
    }
    // Torches can be placed on the sides of blocks.
    // rotation >= 4 encodes attachment face: 4=LEFT, 5=FRONT, 6=RIGHT, 7=BACK
    if ( block->is_torch ) {
        int face = globalGameState.block_selection.face;
        if ( face == FACE_RIGHT ) {
            rotation = 4;       // attached LEFT (solid block is to the left)
        } else if ( face == FACE_FRONT ) {
            rotation = 5;       // attached FRONT (solid block is in front)
        } else if ( face == FACE_LEFT ) {
            rotation = 6;       // attached RIGHT (solid block is to the right)
        } else if ( face == FACE_BACK ) {
            rotation = 7;       // attached BACK (solid block is behind)
        }
    }
    // Pistons can also face up/down: the head points at the player, so looking
    // steeply down places an upward-facing piston, steeply up a downward one.
    if ( block->is_piston ) {
        if ( globalGameState.camera.angle_V > 60 ) {
            rotation = PISTON_ROTATE_UP;
        } else if ( globalGameState.camera.angle_V < -60 ) {
            rotation = PISTON_ROTATE_DOWN;
        }
    }
    return rotation;
}

// static bool block_places_on_all_faces( Block *block ) {
//     for ( int face = FACE_TOP; face < NUM_FACES_IN_CUBE; face++ ) {
//         if ( block->needs_place_on_any_solid[ face ] ) {
//             return false;
//         }
//     }
//     return true;
// }
// Breaks the block under the cursor. In survival mode the mined block is
// added to the hotbar first, then falls back to the survival inventory.

void RepGame::break_selected_block( ) {
    // Debris is spawned by the queued PlayerBlockPlacedEvent, which covers all
    // block-removal paths (mining, support loss, water, remote edits).
    BlockID previous_block = change_block( 0, BLOCK_STATE_AIR );
    if ( previous_block != LAST_BLOCK_ID && previous_block != AIR ) {
        // A real block broke.
        audio_play_sfx( audio_block_dig_pool( previous_block ) );
        if ( globalGameState.vr.active ) {
            // The platform turns this into a haptic pulse.
            globalGameState.vr.haptic_pending = true;
        }
    }
    if ( const NetContainerKindDef *def = net_container_kind_of_block( previous_block ) ) {
        const glm::ivec3 pos = globalGameState.block_selection.pos_destroy;
        if ( globalGameState.multiplayer.is_active( ) ) {
            // The server answers the block removal with a CONTAINER_CONTENTS
            // snapshot to loot — contents are server-authoritative.
            globalGameState.pending_container_loot.insert( pos );
        } else {
            ContainerContents contents;
            if ( globalGameState.world.get_container_contents( def->kind, pos, contents ) ) {
                repgame_loot_container( globalGameState, contents.slots.data( ), static_cast<int>( contents.slots.size( ) ) );
            }
            // The chunk erases the map entry when the block write lands.
        }
        // Close a UI still showing the broken container (one arm per kind —
        // the UI structs stay per-kind even though the sync is generic).
        switch ( def->kind ) {
            case NetContainerKind::CHEST:
                if ( globalGameState.ui.chest.open && globalGameState.ui.chest.pos == pos ) {
                    this->close_chest( );
                    globalGameState.input.inventory_open = false;
                }
                break;
            case NetContainerKind::FURNACE:
                if ( globalGameState.ui.furnace.open && globalGameState.ui.furnace.pos == pos ) {
                    this->close_furnace( );
                    globalGameState.input.inventory_open = false;
                }
                break;
        }
    }
    if ( globalGameState.game_mode == GameMode_Survival && previous_block != LAST_BLOCK_ID && previous_block != AIR ) {
        // Shears harvest leaves as the leaf item itself (natural or
        // persistent — block_item_id maps both to the natural item);
        // everything else rolls the shared drops.
        ItemID force_item = LAST_BLOCK_ID;
        if ( globalGameState.hotbar.getSelectedItem( ) == SHEARS && block_is_leaf( previous_block ) ) {
            force_item = block_item_id( previous_block );
        }
        break_drops( globalGameState, globalGameState.block_selection.pos_destroy, previous_block, force_item );
    }
}

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
        state.ui.pause.set_dead( true );
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
    state.vr.haptic_pending = true;
    return true;
}

// Death-screen exit: stand the player back on the world-spawn column with
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
    v.fall_dist = 0.0;
    CameraState &cam = state.camera;
    cam.horizontal_vel = glm::dvec2( 0.0 );
    cam.y_speed = 0.0;
    cam.pos = glm::dvec3( 0.5, ceil( MapGen::maxTerrainHeight( ) ) + 2.0, 0.5 );
    state.spawn_pending = true;
    cam.snapshot_prev( );
    state.ui.pause.set_dead( false );
    state.input.menu_open = false;
}

void RepGame::respawn( ) {
    repgame_respawn( globalGameState );
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
    const BlockState feet_block = state.world.get_loaded_block( glm::ivec3( glm::round( glm::dvec3( eye.x, feet_y + 0.05, eye.z ) - glm::dvec3( 0.5 ) ) ) );
    const glm::dvec3 body_center( eye.x, feet_y + PLAYER_HEIGHT / 2.0, eye.z );
    const BlockState body_block = state.world.get_loaded_block( glm::ivec3( glm::round( body_center - glm::dvec3( 0.5 ) ) ) );
    // Breath drains while the head is under water (the render path's
    // headInWater test uses the same eye cell) and refills at the surface.
    if ( fluid_in_cell( eye_block ) == WATER ) {
        if ( v.air_ticks > 0 ) {
            v.air_ticks--;
        } else if ( RepGame::player_damage( state, PLAYER_DROWN_DAMAGE ) ) {
            audio_play_sfx( "player_hurt" );
        }
    } else {
        v.air_ticks = PLAYER_AIR_MAX_TICKS;
    }
    // Lava burns fast anywhere it touches the body; open fire burns slowly.
    if ( fluid_in_cell( eye_block ) == LAVA || fluid_in_cell( body_block ) == LAVA || fluid_in_cell( feet_block ) == LAVA ) {
        if ( RepGame::player_damage( state, PLAYER_LAVA_DAMAGE ) ) {
            audio_play_sfx( "player_hurt" );
        }
    } else if ( feet_block.id == FIRE || body_block.id == FIRE || feet_block.id == SOUL_FIRE || body_block.id == SOUL_FIRE ) {
        if ( RepGame::player_damage( state, PLAYER_FIRE_DAMAGE ) ) {
            audio_play_sfx( "player_hurt" );
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

// Fires one arrow from the eye along the look direction: survival draws a
// round from the hotbar first then the survival inventory (no ammo, no
// shot); creative fires free and one-shots. Returns whether it fired.
bool repgame_fire_arrow( RepGameState &state ) {
    const bool survival = state.game_mode == GameMode_Survival;
    if ( survival && !state.hotbar.consumeItem( ARROW, 1 ) && !state.survival_inventory.consumeItem( ARROW, 1 ) ) {
        return false; // Out of arrows.
    }
    // Release from the eye, biased a step along the aim so the flight
    // segment can't collide with the shooter's own head cell.
    const glm::dvec3 dir = glm::dvec3( state.camera.look );
    state.world.arrows.spawn( state.camera.pos + dir * 0.4, dir * ArrowEntities::ARROW_SPEED,
                              survival ? ArrowEntities::ARROW_DAMAGE : 1000.0f, survival ? 1 : 0 );
    audio_play_sfx( "bow_shoot" );
    state.vr.haptic_pending = true;
    return true;
}

// Throws `count` of `item` out of the player as an item entity: spawned just
// ahead of the eye, tossed along the look direction with a little lift, and
void RepGame::process_mouse_events( ) {
    if ( !globalGameState.input.inventory_open && globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle && !globalGameState.input.mouse.buttons.middle_pressed_in_ui && globalGameState.input.click_delay_middle == 0 ) {
        BlockState blockState = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        pr_debug( "Selected block:%d rotation:%d redstone_power:%d display:%d", blockState.id, blockState.rotation, blockState.current_redstone_power, blockState.display_id );
        globalGameState.input.click_delay_middle = 30;
        // A door's upper half picks the lower's item; a crop stage picks the
        // seed/produce item that plants it. A piston head picks the piston
        // it belongs to — the head cell itself isn't obtainable.
        ItemID pick_id = LAST_BLOCK_ID;
        if ( blockState.id != LAST_BLOCK_ID ) {
            if ( block_definition_get_definition( blockState.id )->is_piston_head ) {
                blockState = globalGameState.world.get_loaded_block( piston_head_base( globalGameState.world, globalGameState.block_selection.pos_destroy, blockState ) );
            }
            pick_id = item_for_crop_block( blockState.id );
            if ( pick_id == LAST_BLOCK_ID ) {
                pick_id = block_item_id( blockState.id );
            }
            // A block id that has no obtainable item — an orphaned piston
            // head — picks nothing rather than putting the internal cell
            // block on the bar.
            if ( item_is_block( pick_id ) && !block_definition_get_definition( static_cast<BlockID>( pick_id ) )->is_pickable ) {
                pick_id = LAST_BLOCK_ID;
            }
        }
        if ( globalGameState.game_mode == GameMode_Survival && pick_id != LAST_BLOCK_ID ) {
            // In survival mode, middle-clicking picks the block onto the
            // hotbar. If a non-full stack is already on the bar, just select
            // it (nothing is pulled from the survival inventory); only move a
            // whole stack when the bar's stacks are full (or the block isn't
            // on the bar at all).
            int bar_slot = globalGameState.hotbar.findSlotWithItem( pick_id, true );
            if ( bar_slot < 0 ) {
                globalGameState.survival_inventory.moveItemToHotbar( pick_id, globalGameState.hotbar, true );
                // The moved stack, or an existing (full) stack on the bar.
                bar_slot = globalGameState.hotbar.findSlotWithItem( pick_id, false );
            }
            if ( bar_slot >= 0 ) {
                globalGameState.hotbar.setSelectedSlot( bar_slot );
            }
            // Debug feature: also give 1 of the picked block so spamming
            // middle-click restocks the bar without mining.
            globalGameState.hotbar.addItemWithQuantity( pick_id, 1 );
            if ( bar_slot < 0 ) {
                // Nothing was on the bar: the restock may have created a
                // stack elsewhere. Select it so pick-block still works.
                const int restock_slot = globalGameState.hotbar.findSlotWithItem( pick_id, false );
                if ( restock_slot >= 0 ) {
                    globalGameState.hotbar.setSelectedSlot( restock_slot );
                }
            }
            ItemID selectedBlock = globalGameState.hotbar.getSelectedItem( );
            globalGameState.ui.overlay.set_holding_item( selectedBlock );
        } else if ( pick_id != LAST_BLOCK_ID ) {
            // Creative mode: always give the block.
            RepGame::add_to_hotbar( true, pick_id );
        }
    }
    // Mob hits pre-empt mining: a mob nearer than the selected block (or no
    // block selected) takes the left-click damage instead. The raycast is
    // recomputed here rather than trusting block_selection.target_mob —
    // mobs.tick runs between the selection resolution and this call and may
    // have removed mobs, so a stored index can go stale.
    int hit_mob = -1;
    if ( globalGameState.input.mouse.buttons.left && !globalGameState.input.mouse.buttons.left_pressed_in_ui && !globalGameState.input.inventory_open && globalGameState.input.click_delay_left == 0 ) {
        hit_mob = globalGameState.world.mobs.resolve_target( globalGameState.block_selection.ray_origin, globalGameState.block_selection.ray_dir, REACH_DISTANCE,
                                                           globalGameState.block_selection.selectionInBounds, globalGameState.block_selection.pos_destroy, nullptr );
    }
    if ( hit_mob >= 0 ) {
        globalGameState.input.click_delay_left = 30;
        globalGameState.block_mining.progress_ticks = 0.0f;
        globalGameState.vr.haptic_pending = true;
        if ( globalGameState.world.mobs.net_mirror_at( hit_mob ) ) {
            // A remote-owned mob: flash + sfx now, damage lands via
            // ENTITY_ACTION on the owner.
            globalGameState.world.mobs.mirror_hit_feedback( hit_mob, globalGameState.camera.pos );
            NetEntityActionPayload act;
            act.id = globalGameState.world.mobs.net_id_at( hit_mob );
            act.action = NET_ACTION_HIT;
            act.damage = globalGameState.game_mode == GameMode_Survival ? HIT_DAMAGE : 1000.0f;
            act.ax = globalGameState.camera.pos.x;
            act.ay = globalGameState.camera.pos.y;
            act.az = globalGameState.camera.pos.z;
            act.attacker_survival = globalGameState.game_mode == GameMode_Survival;
            act.item = 0;
            globalGameState.multiplayer.entities.send_entity_action( act );
        } else {
            // Survival: HIT_DAMAGE per click; creative kills in one hit. A
            // survival kill hands the type's drops to the player's
            // inventories — babies never drop.
            const bool killed = globalGameState.world.mobs.hurt( hit_mob, globalGameState.game_mode == GameMode_Survival ? HIT_DAMAGE : 1000.0f,
                                                                 globalGameState.camera.pos, globalGameState.camera.pos );
            if ( killed && globalGameState.game_mode == GameMode_Survival && !globalGameState.world.mobs.is_baby_at( hit_mob ) ) {
                spawn_mob_drops( globalGameState, globalGameState.world.mobs.def_at( hit_mob ), globalGameState.world.mobs.pos_at( hit_mob ) );
            }
        }
    }
    // Mob use-item interactions pre-empt the block ones: a right-click aimed
    // at a mob while holding one of its use-items applies it — a breed food
    // feeds it, shears shear an unshorn sheep. A remote-owned mob gets
    // ENTITY_ACTION — the owner validates the claimed item and applies.
    int interact_mob = -1;
    uint8_t interact_action = NET_ACTION_FEED;
    if ( globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui &&
         !globalGameState.input.inventory_open && globalGameState.input.click_delay_right == 0 ) {
        const ItemID held = globalGameState.hotbar.getSelectedItem( );
        const int target = globalGameState.world.mobs.resolve_target( globalGameState.block_selection.ray_origin, globalGameState.block_selection.ray_dir, REACH_DISTANCE,
                                                                      globalGameState.block_selection.selectionInBounds, globalGameState.block_selection.pos_destroy, nullptr );
        if ( target >= 0 && globalGameState.world.mobs.shearable( target, held ) ) {
            interact_mob = target;
            interact_action = NET_ACTION_SHEAR;
        } else if ( target >= 0 && globalGameState.world.mobs.feedable( target, held ) ) {
            interact_mob = target;
        }
    }
    if ( interact_mob >= 0 ) {
        const ItemID held = globalGameState.hotbar.getSelectedItem( );
        globalGameState.input.click_delay_right = 30;
        globalGameState.vr.haptic_pending = true;
        if ( globalGameState.world.mobs.net_mirror_at( interact_mob ) ) {
            NetEntityActionPayload act;
            act.id = globalGameState.world.mobs.net_id_at( interact_mob );
            act.action = interact_action;
            act.damage = 0.0f;
            act.ax = globalGameState.camera.pos.x;
            act.ay = globalGameState.camera.pos.y;
            act.az = globalGameState.camera.pos.z;
            act.attacker_survival = globalGameState.game_mode == GameMode_Survival;
            act.item = static_cast<uint32_t>( held );
            globalGameState.multiplayer.entities.send_entity_action( act );
        } else if ( interact_action == NET_ACTION_SHEAR ) {
            if ( globalGameState.world.mobs.shear( interact_mob, held, globalGameState.camera.pos ) ) {
                spawn_mob_shear_drop( globalGameState, globalGameState.world.mobs.def_at( interact_mob ), globalGameState.world.mobs.pos_at( interact_mob ) );
            }
        } else {
            globalGameState.world.mobs.feed( interact_mob, held );
        }
        // Feeding consumes the food; shears are a stack-1 tool and keep.
        if ( interact_action == NET_ACTION_FEED && globalGameState.game_mode == GameMode_Survival ) {
            globalGameState.hotbar.consumeSelected( 1 );
            globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
        }
    }
    const bool holding_break = hit_mob < 0 && globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left && !globalGameState.input.mouse.buttons.left_pressed_in_ui;
    if ( holding_break && globalGameState.input.click_delay_left == 0 ) {
        // Mining a block
        if ( globalGameState.game_mode == GameMode_Survival ) {
            // Survival mode: blocks take time to break. Accumulate progress
            // each tick the button is held on the same block.
            const glm::ivec3 target_pos = globalGameState.block_selection.pos_destroy;
            const BlockState target_state = globalGameState.world.get_loaded_block( target_pos );
            const Block *target_def = target_state.id == LAST_BLOCK_ID ? nullptr : block_definition_get_definition( target_state.id );
            // hardness < 0 is unbreakable (bedrock/barrier); 0 breaks instantly.
            if ( target_def != nullptr && target_def->hardness >= 0.0f ) {
                // Aiming at a different block (or the block at the position
                // changing) restarts progress.
                if ( globalGameState.block_mining.pos != target_pos || globalGameState.block_mining.id != target_state.id ) {
                    globalGameState.block_mining.progress_ticks = 0.0f;
                }
                globalGameState.block_mining.pos = target_pos;
                globalGameState.block_mining.id = target_state.id;
                // A matching tool (pickaxe on stone, axe on wood, ...) adds
                // its tier multiplier per tick instead of 1.
                globalGameState.block_mining.progress_ticks += item_mining_speed( globalGameState.hotbar.getSelectedItem( ), target_state.id );
                if ( globalGameState.block_mining.progress_ticks >= target_def->hardness * static_cast<float>( UPS_RATE ) ) {
                    break_selected_block( );
                    globalGameState.block_mining.progress_ticks = 0.0f;
                    globalGameState.input.click_delay_left = 30;
                }
            }
        } else {
            // Creative mode breaks instantly.
            break_selected_block( );
            globalGameState.input.click_delay_left = 30;
        }
    }
    if ( !holding_break ) {
        // Released the mouse or looked away: abandon mining progress.
        globalGameState.block_mining.progress_ticks = 0.0f;
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_button ) {
            // Right-click presses a button instead of placing against it:
            // powered for a timeout, then released. Wood stays pressed ~1.5s,
            // stone ~1s. The click delay also swallows the placement attempt
            // below.
            const long press_ticks = target_state.id == STONE_BUTTON ? UPS_RATE : UPS_RATE + UPS_RATE / 2;
            BlockState released_state = target_state;
            released_state.current_redstone_power = 0;
            if ( target_state.current_redstone_power == 0 ) {
                BlockState pressed_state = target_state;
                pressed_state.current_redstone_power = REDSTONE_SOURCE_POWER;
                globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, pressed_state, true );
            }
            // Always (re)queue the release: idempotent while unpressed, and it
            // revives a button saved while pressed (queued events don't
            // persist across loads).
            globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number + press_ticks, globalGameState.block_selection.pos_destroy, released_state, true );
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Right-clicking a chest opens it instead of placing against it.
        // Sneaking skips the interaction so the block can be placed.
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( block_is_chest( target_state.id ) && !globalGameState.input.movement.sneakPressed ) {
            this->open_chest( globalGameState.block_selection.pos_destroy );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-clicking a crafting table opens the 3x3 grid instead of
        // placing against it. Sneaking skips the interaction.
        if ( target_state.id == CRAFTING_TABLE && !globalGameState.input.movement.sneakPressed ) {
            globalGameState.ui.crafting.table_open = true;
            globalGameState.ui.crafting.table_pos = globalGameState.block_selection.pos_destroy;
            globalGameState.input.inventory_open = true;
            repgame_crafting_layout( globalGameState );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-clicking a furnace (lit or unlit) opens its smelt UI instead
        // of placing against it. Sneaking skips the interaction.
        if ( block_is_furnace( target_state.id ) && !globalGameState.input.movement.sneakPressed ) {
            this->open_furnace( globalGameState.block_selection.pos_destroy );
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Right-clicking a fence gate toggles it open/closed instead of
        // placing against it. Sneaking skips the interaction so the block
        // can be placed.
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_fence_gate && !globalGameState.input.movement.sneakPressed ) {
            BlockState toggled_state = target_state;
            toggled_state.rotation = fence_gate_toggle_rotation( target_state.rotation, getPlacedRotation( target_state.id ) );
            globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, toggled_state, true );
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Right-clicking a door toggles it open/closed like a fence gate.
        // Iron doors are redstone-only and fall through to placement.
        // Sneaking skips the interaction so the block can be placed.
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( target_state.id != LAST_BLOCK_ID && !globalGameState.input.movement.sneakPressed ) {
            const Block *target = block_definition_get_definition( target_state.id );
            if ( target->is_door && !target->door_redstone_only ) {
                globalGameState.blockUpdateQueue.addBlockUpdate<DoorEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, DoorEvent::TOGGLE );
                globalGameState.input.click_delay_right = 30;
            }
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Right-clicking a cake eats one 2px slice — the bite count lives in
        // rotation (0..CAKE_MAX_BITES) and shrinks the box off the +x edge.
        // Eating the last slice removes the block like a break (no drop).
        // Sneaking skips the interaction so a block can be placed.
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_cake && !globalGameState.input.movement.sneakPressed ) {
            if ( target_state.rotation < CAKE_MAX_BITES ) {
                BlockState eaten_state = target_state;
                eaten_state.rotation = static_cast<unsigned char>( target_state.rotation + 1 );
                globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, eaten_state, true );
            } else {
                // The last slice is eaten, not broken: no drop, no debris.
                globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, BLOCK_STATE_AIR, false,
                                                                                       BreakDrops::None, BreakDebris::None );
            }
            audio_play_sfx( audio_block_dig_pool( target_state.id ) );
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( ( globalGameState.block_selection.selectionInBounds || globalGameState.block_selection.fluidSelectionInBounds ) && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Buckets scoop a fluid source or pour theirs out — after the block
        // interactions above (they need the normal pick too), before the
        // use/placement paths below. Buckets are items so nothing below can
        // place them anyway, and they aren't sneak-gated — there's no
        // place-against-interactable conflict to unlock. repgame_use_bucket
        // owns the click delay.
        repgame_use_bucket( globalGameState );
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Bone meal on a crop advances it one stage (the chain ends at the
        // mature stage, which has no crop_next) and on a sapling grows it
        // immediately — before the placement path below, and skipped while
        // sneaking like the other block interactions. It ignores the light
        // check random ticks apply; only a successful grow consumes one.
        if ( globalGameState.hotbar.getSelectedItem( ) == BONE_MEAL && !globalGameState.input.movement.sneakPressed ) {
            const glm::ivec3 &pos = globalGameState.block_selection.pos_destroy;
            const BlockState target_state = globalGameState.world.get_loaded_block( pos );
            const Block *target = target_state.id == LAST_BLOCK_ID ? nullptr : block_definition_get_definition( target_state.id );
            bool grown = false;
            if ( target != nullptr && target->crop_next != LAST_BLOCK_ID ) {
                repgame_convert_block( globalGameState, pos, target->crop_next );
                grown = true;
            } else {
                TreeSpec spec;
                grown = target != nullptr && tree_spec_for_sapling( target_state.id, pos, &spec ) && tree_grow_try( globalGameState, pos );
            }
            if ( grown ) {
                audio_play_sfx( audio_block_dig_pool( target_state.id ) );
                if ( globalGameState.game_mode == GameMode_Survival ) {
                    globalGameState.hotbar.consumeSelected( 1 );
                    globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                }
                globalGameState.input.click_delay_right = 30;
            }
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // A hoe tills the clicked soil block — grass/dirt/path to farmland,
        // podzol/mycelium/coarse dirt to plain dirt — but only when the cell
        // above is open (MC won't till under a standing crop or block).
        if ( item_is_hoe( globalGameState.hotbar.getSelectedItem( ) ) && !globalGameState.input.movement.sneakPressed ) {
            const glm::ivec3 &pos = globalGameState.block_selection.pos_destroy;
            const BlockState target_state = globalGameState.world.get_loaded_block( pos );
            BlockID tilled = LAST_BLOCK_ID;
            switch ( target_state.id ) {
                case GRASS:
                case SNOWY_GRASS:
                case DIRT:
                case GRASS_PATH:
                    tilled = FARMLAND;
                    break;
                case PODZOL:
                case MYCELIUM:
                case COARSE_DIRT:
                    tilled = DIRT;
                    break;
                default:
                    break;
            }
            const BlockState above_state = globalGameState.world.get_loaded_block( pos + glm::ivec3( 0, 1, 0 ) );
            if ( tilled != LAST_BLOCK_ID && above_state.id != LAST_BLOCK_ID &&
                 block_definition_get_definition( above_state.id )->can_be_placed_in ) {
                repgame_convert_block( globalGameState, pos, tilled );
                audio_play_sfx( audio_block_dig_pool( target_state.id ) );
                globalGameState.input.click_delay_right = 30;
            }
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // A spawn egg hatches its mob in the cell a placed block would go —
        // the one adjacent to the clicked face — facing the player. The
        // block interactions above (chests, doors, ...) take precedence;
        // sneaking skips them and reaches here like placement does.
        const MobType egg_type = mob_type_for_spawn_egg( globalGameState.hotbar.getSelectedItem( ) );
        if ( egg_type != MOB_TYPE_COUNT ) {
            const glm::dvec3 pos = glm::dvec3( globalGameState.block_selection.pos_create ) + glm::dvec3( 0.5, 0.0, 0.5 );
            const glm::dvec3 to_player = globalGameState.camera.pos - pos;
            // Mob yaw convention: forward = (sin yaw, 0, -cos yaw), degrees.
            const float yaw = glm::degrees( atan2f( static_cast<float>( to_player.x ), static_cast<float>( -to_player.z ) ) );
            // Explicit player spawn: uncapped — the egg always hatches.
            if ( globalGameState.world.mobs.spawn( egg_type, pos, yaw, 1, false ) ) {
                if ( globalGameState.game_mode == GameMode_Survival ) {
                    globalGameState.hotbar.consumeSelected( 1 );
                    globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                }
                globalGameState.input.click_delay_right = 30;
            } else {
                pr_debug( "Spawn egg failed: mobs disabled" );
            }
        }
    }
    if ( globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && !globalGameState.input.inventory_open && globalGameState.input.click_delay_right == 0 ) {
        // Right-click with the bow held releases an arrow along the look
        // direction — after the block interactions above (a chest aimed at
        // still opens), outside the placement path (the bow is a pure item
        // and fires at air too).
        if ( globalGameState.hotbar.getSelectedItem( ) == BOW && repgame_fire_arrow( globalGameState ) ) {
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && !globalGameState.input.inventory_open && globalGameState.input.click_delay_right == 0 ) {
        // Right-click with food held eats one instantly (beta-style heal)
        // — same tier as the bow: after the block interactions above and
        // at air too. Full health refuses so food isn't wasted.
        if ( repgame_eat_food( globalGameState ) ) {
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( ( globalGameState.block_selection.selectionInBounds || globalGameState.block_selection.floatSelectionInBounds ) && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Placing a block — non-block items can't be placed, but "plants"
        // items (seeds/produce) place their stage-0 crop like a block: the
        // adjacent cell must be open and the survive rule — crops need
        // farmland below — does the field check.
        const ItemID held_item = globalGameState.hotbar.getSelectedItem( );
        const BlockID holdingBlock = item_is_block( held_item ) ? static_cast<BlockID>( held_item ) : LAST_BLOCK_ID;
        const BlockID plant_id = item_plant_block( held_item );
        if ( plant_id != LAST_BLOCK_ID ) {
            bool can_place = true;
            if ( globalGameState.game_mode == GameMode_Survival ) {
                // canPlaceSelected only covers block items; seeds are pure
                // items, so check the stack directly.
                can_place = globalGameState.hotbar.getSelectedQuantity( ) > 0;
            }
            if ( can_place ) {
                const BlockID placed_block = change_block( 1, { plant_id, BLOCK_ROTATE_0, 0, plant_id, 0 } );
                if ( placed_block != LAST_BLOCK_ID ) {
                    audio_play_sfx( audio_block_place_pool( plant_id ) );
                    if ( globalGameState.game_mode == GameMode_Survival ) {
                        globalGameState.hotbar.consumeSelected( 1 );
                        globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                    }
                }
                globalGameState.input.click_delay_right = 30;
            }
        } else if ( holdingBlock != LAST_BLOCK_ID ) {
            const Block *holdingBlockDef = block_definition_get_definition( holdingBlock );
            bool did_toggle_dust = false;

            // Toggle redstone dust between cross and dot when right-clicking an existing dust block with dust in hand
            if ( holdingBlockDef->is_redstone_dust ) {
                BlockState targetBlock = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
                if ( targetBlock.id != LAST_BLOCK_ID && block_definition_get_definition( targetBlock.id )->is_redstone_dust ) {
                    BlockState new_block_state = targetBlock;
                    if ( targetBlock.display_id == REDSTONE_WIRE_CROSS ) {
                        new_block_state.display_id = REDSTONE_WIRE_DOT;
                    } else if ( targetBlock.display_id == REDSTONE_WIRE_DOT ) {
                        new_block_state.display_id = REDSTONE_WIRE_CROSS;
                    } else {
                        // Has connections, can't toggle — fall through to place new dust
                        new_block_state.display_id = LAST_BLOCK_ID; // sentinel: don't toggle
                    }
                    if ( new_block_state.display_id != LAST_BLOCK_ID ) {
                        globalGameState.blockUpdateQueue.addBlockUpdate<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, new_block_state, true );
                        globalGameState.input.click_delay_right = 30;
                        did_toggle_dust = true;
                    }
                }
            }

            if ( !did_toggle_dust ) {
                const unsigned char rotation = getPlacedRotation( holdingBlock );
                if ( holdingBlockDef->is_torch ) {
                    pr_debug( "Placing torch face:%d rotation:%d", globalGameState.block_selection.face, rotation );
                }
                // In survival mode, check that we have a block to place and consume it.
                bool can_place = true;
                if ( globalGameState.game_mode == GameMode_Survival ) {
                    can_place = globalGameState.hotbar.canPlaceSelected( );
                }
                if ( can_place ) {
                    if ( holdingBlockDef->can_float_on_water && globalGameState.block_selection.floatSelectionInBounds ) {
                        globalGameState.block_selection.pos_create = globalGameState.block_selection.pos_create_water;
                    }
                    BlockID placed_block = change_block( 1, { holdingBlock, rotation, 0, holdingBlock, 0 } );
                    if ( placed_block != LAST_BLOCK_ID ) {
                        audio_play_sfx( audio_block_place_pool( holdingBlock ) );
                    }
                    if ( placed_block != LAST_BLOCK_ID && globalGameState.game_mode == GameMode_Survival ) {
                        globalGameState.hotbar.consumeSelected( 1 );
                        ItemID newHolding = globalGameState.hotbar.getSelectedItem( );
                        globalGameState.ui.overlay.set_holding_item( newHolding );
                    }
                    globalGameState.input.click_delay_right = 30;
                }
            }
        }
    }
    // if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.currentPosition.wheel_counts != globalGameState.input.mouse.previousPosition.wheel_counts ) {
    //     int wheel_diff = globalGameState.input.mouse.currentPosition.wheel_counts > globalGameState.input.mouse.previousPosition.wheel_counts ? 1 : -1;
    //     BlockState blockState = world_get_loaded_block( &globalGameState.world, globalGameState.block_selection.pos_destroy );
    //     int blockID_int = ( int )blockState.id;
    //     if ( blockID_int != LAST_BLOCK_ID ) {
    //         bool valid_scroll_block;
    //         do {
    //             blockID_int += wheel_diff;
    //             if ( blockID_int >= LAST_BLOCK_ID ) {
    //                 blockID_int = 1;
    //             }
    //             if ( blockID_int <= 0 ) {
    //                 blockID_int = LAST_BLOCK_ID - 1;
    //             }
    //             Block *next_block = block_definition_get_definition( ( BlockID )blockID_int );
    //             bool is_pickable = next_block->is_pickable;
    //             bool places_on_any_face = block_places_on_all_faces( next_block );
    //             valid_scroll_block = places_on_any_face && is_pickable;
    //         } while ( !valid_scroll_block );
    //         blockState.id = ( BlockID )blockID_int;
    //         blockState.display_id = ( BlockID )blockID_int;
    //         // blockState.rotation = getPlacedRotation( blockState.id );
    //         change_block( 0, BLOCK_STATE_AIR );
    //         change_block( 0, blockState );
    //     }
    // }
    globalGameState.input.mouse.currentPosition.wheel_counts = 0;
}

void RepGame::build_camera_view( float angle_H, float angle_V, const glm::dvec3 &pos, glm::vec3 &look, glm::mat4 &rotation, glm::mat4 &view_look, glm::mat4 &view_trans ) const {
    look = glm::normalize( glm::vec3(        //
        sin( ( angle_H ) * ( M_PI / 180 ) ), //
        -tan( angle_V * ( M_PI / 180 ) ),    //
        -cos( ( angle_H ) * ( M_PI / 180 ) ) ) );

    glm::mat4 rotate = glm::rotate( glm::mat4( 1.0f ), glm::radians( -angle_H + 180 ), glm::vec3( 0.0f, 1.0f, 0.0f ) );
    rotate = glm::rotate( rotate, glm::radians( angle_V ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    rotation = rotate;

    view_look = glm::lookAt( glm::vec3( 0.0f, 0.0f, 0.0f ), // From the origin
                             look,                         // Look at look vector
                             glm::vec3( 0.0f, 1.0f, 0.0f )  // Head is up (set to 0,-1,0 to look upside-down)
    );
    view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -pos ) );
}

void RepGame::process_camera_angle( ) {
    float upAngleLimit = 90 - 0.001f;
    if ( globalGameState.camera.angle_V > upAngleLimit ) {
        globalGameState.camera.angle_V = upAngleLimit;
    }
    float downAngleLimit = -90 + 0.001f;
    if ( globalGameState.camera.angle_V < downAngleLimit ) {
        globalGameState.camera.angle_V = downAngleLimit;
    }
    build_camera_view( globalGameState.camera.angle_H, globalGameState.camera.angle_V, globalGameState.camera.pos, //
                       globalGameState.camera.look, globalGameState.camera.rotation, globalGameState.camera.view_look, globalGameState.camera.view_trans );

    globalGameState.camera.movement.x = sinf( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * static_cast<float>( ( M_PI / 180 ) ) );
    globalGameState.camera.movement.y = -tanf( globalGameState.camera.angle_V * static_cast<float>( ( M_PI / 180 ) ) );
    globalGameState.camera.movement.z = -cosf( ( globalGameState.camera.angle_H + globalGameState.input.movement.angleH ) * static_cast<float>( ( M_PI / 180 ) ) );
}

void RepGame::process_movement( ) {
    PROFILE_SCOPE( "player_movement" );
    bool player_flying = globalGameState.input.player_flying;
    bool player_sprinting = globalGameState.input.player_sprinting;

    // Swimming engages only when the body center is *below the surface plane*
    // of a liquid block — water tops out at WATER_HEIGHT within its block, so
    // in 1-deep water the center sits above the surface and the player wades
    // (slowed walk, normal jump) instead of swimming. camera.pos is the eye;
    // feet are eye_height below it. The swim check uses the STANDING body
    // center (feet + PLAYER_HEIGHT/2) so it is pose-independent — a crouched
    // player dipping their actual center below the surface must not flip into
    // swimming, which would cancel the sneak and oscillate every tick.
    const double body_center_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height + PLAYER_HEIGHT / 2.0;
    const glm::dvec3 body_center( globalGameState.camera.pos.x, body_center_y, globalGameState.camera.pos.z );
    const glm::ivec3 body_block_pos = glm::ivec3( glm::round( body_center - glm::dvec3( 0.5 ) ) );
    const BlockState body_block = globalGameState.world.get_loaded_block( body_block_pos );
    const bool body_in_liquid = body_block.id < LAST_BLOCK_ID && block_definition_get_definition( body_block.id )->flows != 0;
    // A water block is only partially full when it's the surface: if the block
    // above is also liquid, the column fills it completely.
    const BlockState above_block = globalGameState.world.get_loaded_block( body_block_pos + glm::ivec3( 0, 1, 0 ) );
    const bool liquid_above = above_block.id < LAST_BLOCK_ID && block_definition_get_definition( above_block.id )->flows != 0;
    // Sloped fluids sit lower than the source height — the surface lookup
    // handles level heights and full columns, falling back to WATER_HEIGHT
    // for non-fluid cells.
    const double surface_height = static_cast<double>( body_block_pos.y ) + ( body_in_liquid ? fluid_surface_height( globalGameState.world, body_block_pos ) : WATER_HEIGHT );
    const bool player_swimming = !player_flying && body_in_liquid && ( liquid_above || body_center.y < surface_height );
    // Wading: feet in a liquid but body above the surface (e.g. 1-deep water).
    const glm::ivec3 feet_block_pos = glm::ivec3( glm::round( globalGameState.camera.pos - glm::dvec3( 0.0, globalGameState.camera.eye_height - 0.05, 0.0 ) - glm::dvec3( 0.5 ) ) );
    const BlockState feet_block = globalGameState.world.get_loaded_block( feet_block_pos );
    const bool player_wading = !player_flying && !player_swimming && feet_block.id < LAST_BLOCK_ID && block_definition_get_definition( feet_block.id )->flows != 0;

    // Climbing (ladders): the player is "on a ladder" while the body AABB
    // overlaps a cell holding a climbable block. The check uses the standing
    // height (like the swim check's standing body center) so a sneak-pose
    // transition can't drop the grip mid-tick.
    const bool player_climbing = !player_flying && !globalGameState.input.no_clip &&
                                 Collision::on_climbable( globalGameState.world, globalGameState.camera.pos, PLAYER_HEIGHT, globalGameState.camera.eye_height );

    // Sneak pose transitions. The sneak key doubles as "move down" while
    // flying, and while swimming it crouches AND sinks (the swim branch below
    // still applies its sink assist). Feet stay planted: the eye
    // (camera.pos.y) shifts by the eye-height difference.
    const bool wants_sneak = globalGameState.input.movement.sneakPressed && !player_flying && !globalGameState.input.no_clip;
    if ( wants_sneak && !globalGameState.camera.sneaking ) {
        globalGameState.camera.sneaking = 1;
        globalGameState.camera.player_height = PLAYER_SNEAK_HEIGHT;
        globalGameState.camera.eye_height = PLAYER_SNEAK_EYE_HEIGHT;
        globalGameState.camera.pos.y -= PLAYER_SNEAK_EYE_DROP;
    } else if ( !wants_sneak && globalGameState.camera.sneaking ) {
        // Stand up only if the taller box has headroom; otherwise stay
        // crouched. Rechecked every tick, so the player stands as soon as
        // they walk out from under a low ceiling.
        const glm::dvec3 stand_eye = globalGameState.camera.pos + glm::dvec3( 0.0, PLAYER_SNEAK_EYE_DROP, 0.0 );
        if ( !Collision::collides_at( globalGameState.world, stand_eye, PLAYER_HEIGHT, PLAYER_EYE_HEIGHT ) ) {
            globalGameState.camera.sneaking = 0;
            globalGameState.camera.player_height = PLAYER_HEIGHT;
            globalGameState.camera.eye_height = PLAYER_EYE_HEIGHT;
            globalGameState.camera.pos.y += PLAYER_SNEAK_EYE_DROP;
        }
    }
    // Ease the rendered eye height toward the pose target so the camera
    // glides instead of snapping; applied in draw(), not used by physics.
    globalGameState.camera.eye_height_render += ( globalGameState.camera.eye_height - globalGameState.camera.eye_height_render ) * 0.3;

    float gravity = player_flying ? 0 : ( player_swimming ? WATER_GRAVITY_STRENGTH : GRAVITY_STRENGTH );

    float movement_speed;
    if ( player_flying && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_FLYING_SPRINTING;
    } else if ( player_flying && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_FLYING;
    } else if ( player_swimming && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SWIMMING_SPRINTING;
    } else if ( player_swimming && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SWIMMING;
    } else if ( globalGameState.camera.sneaking && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SNEAKING_SPRINTING;
    } else if ( globalGameState.camera.sneaking ) {
        movement_speed = MOVEMENT_SENSITIVITY_SNEAKING;
    } else if ( player_wading && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_WADING_SPRINTING;
    } else if ( player_wading ) {
        movement_speed = MOVEMENT_SENSITIVITY_WADING;
    } else if ( !player_flying && player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_SPRINTING;
    } else if ( !player_flying && !player_sprinting ) {
        movement_speed = MOVEMENT_SENSITIVITY_WALKING;
    } else {
        movement_speed = 0;
        pr_debug( "Invalid sprint fly combo" );
    }

    if ( player_flying ) {
        const float fly_vspeed = player_sprinting ? FLY_SPRINT_VERTICAL_SPEED : FLY_VERTICAL_SPEED;
        if ( globalGameState.input.movement.sneakPressed && globalGameState.input.movement.jumpPressed ) {
            globalGameState.camera.y_speed = 0;
        } else {
            if ( globalGameState.input.movement.sneakPressed ) {
                globalGameState.camera.y_speed = -fly_vspeed;
            } else if ( globalGameState.input.movement.jumpPressed ) {
                globalGameState.camera.y_speed = fly_vspeed;
            } else {
                globalGameState.camera.y_speed = 0;
            }
        }
    } else if ( player_climbing ) {
        // On a ladder (a cell whose block is "climbable"): gravity still
        // applies, so idle players slide down capped at CLIMB_SLIDE_SPEED
        // while upward momentum (jumping past the cell) is preserved.
        // Holding jump, or pushing FORWARD into a wall that blocks the move
        // (MC's horizontalCollision — input held while the collision-resolved
        // horizontal velocity is ~0), ascends; sneak hangs in place.
        // Strafe/back input into a wall does NOT climb: angleH is relative
        // to the camera yaw (W=0, D=+90, S=180, A=-90), so cos > 0.5 keeps
        // forward and the ±45 diagonals but drops side and back.
        const bool pushing = globalGameState.input.movement.sizeH > 0.001f &&
                             cosf( globalGameState.input.movement.angleH * static_cast<float>( M_PI / 180.0 ) ) > 0.5f &&
                             glm::length( globalGameState.camera.horizontal_vel ) < 0.01;
        if ( globalGameState.input.movement.jumpPressed || pushing ) {
            globalGameState.camera.y_speed = CLIMB_VERTICAL_SPEED;
        } else if ( globalGameState.input.movement.sneakPressed ) {
            globalGameState.camera.y_speed = 0;
            gravity = 0; // hard hang — gravity's substep can't pull below 0
        } else if ( globalGameState.camera.y_speed < -CLIMB_SLIDE_SPEED ) {
            globalGameState.camera.y_speed = -CLIMB_SLIDE_SPEED;
        }
    } else if ( player_swimming ) {
        // Vertical control is only jump (swim up) and sneak (sink faster) —
        // pitch does not steer, so forward input moves purely horizontally
        // like walking. Idle players drift toward a gentle sink: weak water
        // gravity plus the drag below settles y_speed near
        // -WATER_GRAVITY_STRENGTH / (1 - WATER_VERTICAL_DRAG).
        double swim_y_target = 0.0;
        if ( globalGameState.input.movement.jumpPressed ) {
            swim_y_target += SWIM_VERTICAL_SPEED;
        }
        if ( globalGameState.input.movement.sneakPressed ) {
            swim_y_target -= SWIM_SINK_SPEED;
        }
        if ( fabs( swim_y_target ) > 1e-4 ) {
            const double dy = swim_y_target - globalGameState.camera.y_speed;
            globalGameState.camera.y_speed += glm::clamp( dy, -static_cast<double>( PLAYER_WATER_ACCEL ), static_cast<double>( PLAYER_WATER_ACCEL ) );
        } else {
            globalGameState.camera.y_speed *= WATER_VERTICAL_DRAG;
        }
    } else {
        if ( globalGameState.input.movement.jumpPressed && globalGameState.camera.standing_on_solid && globalGameState.camera.y_speed < 0.01f && globalGameState.camera.y_speed > -0.01f ) {
            globalGameState.camera.y_speed = JUMP_STRENGTH;
        }
    }

    float accel = -gravity;

    // Build the target horizontal velocity from input. movement.x/z are the
    // unit direction derived from the camera yaw plus the stick's relative
    // angleH (angleH convention: W=0, D=+90, S=180, A=-90), so pitch never
    // steers — swimming moves on the yaw plane just like walking. sizeH is
    // the analog input magnitude (0..1 on keyboard). The target is clamped
    // to the selected movement speed so acceleration can never push us past
    // it.
    const float target_vx = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.x;
    const float target_vz = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.z;
    const glm::dvec2 target_vel( target_vx, target_vz );

    // Pick the rate at which horizontal_vel approaches target_vel. When input
    // is held we accelerate; when it is released we apply (usually higher)
    // friction to bring the player to rest. Flying uses a high rate so it
    // stays snappy and effectively reaches target in a single tick. Water
    // uses a low rate for a damped, drifting feel.
    const bool has_input = globalGameState.input.movement.sizeH > 0.001f;
    float rate;
    if ( player_flying ) {
        rate = PLAYER_FLY_ACCEL;
    } else if ( player_swimming ) {
        rate = has_input ? PLAYER_WATER_ACCEL : PLAYER_WATER_FRICTION;
    } else if ( has_input ) {
        rate = globalGameState.camera.standing_on_solid ? PLAYER_GROUND_ACCEL : PLAYER_AIR_ACCEL;
    } else {
        rate = globalGameState.camera.standing_on_solid ? PLAYER_GROUND_FRICTION : PLAYER_AIR_FRICTION;
    }

    // Accelerate horizontal_vel toward target_vel, clamping the per-tick
    // velocity change to `rate` so the approach is smooth.
    glm::dvec2 dvel = target_vel - globalGameState.camera.horizontal_vel;
    double dvel_len = glm::length( dvel );
    if ( dvel_len <= rate || rate <= 0.0f ) {
        globalGameState.camera.horizontal_vel = target_vel;
    } else {
        globalGameState.camera.horizontal_vel += dvel * ( rate / dvel_len );
    }

    glm::dvec3 movement_vector = glm::dvec3( );
    movement_vector.x = globalGameState.camera.horizontal_vel.x;
    movement_vector.y = globalGameState.camera.y_speed + accel;
    // Terminal velocity only applies to gravity-driven falling, not to flying
    // (which sets y_speed directly from input). Swimming has its own much
    // lower cap.
    if ( !player_flying ) {
        const double terminal = player_swimming ? SWIM_TERMINAL_VELOCITY : TERMINAL_VELOCITY;
        if ( movement_vector.y > terminal ) {
            movement_vector.y = terminal;
        }
        if ( movement_vector.y < -terminal ) {
            movement_vector.y = -terminal;
        }
    }
    movement_vector.z = globalGameState.camera.horizontal_vel.y;
    if ( globalGameState.input.no_clip ) {
        globalGameState.camera.standing_on_solid = 0;
    } else {
        Collision::check_move( globalGameState.world, movement_vector,                                     //
                               globalGameState.camera.pos,                                                 //
                               &globalGameState.camera.standing_on_solid,                                  //
                               globalGameState.camera.player_height, globalGameState.camera.eye_height,    //
                               globalGameState.camera.sneaking != 0 );
    }

    if ( run_policy( ).walktest ) {
        fprintf( stderr, "WT %ld pos %.5f %.5f %.5f req %.5f %.5f applied %.5f %.5f %.5f yspeed %.5f standing %d angle %.2f\n", //
                 globalGameState.tick_number, globalGameState.camera.pos.x, globalGameState.camera.pos.y, globalGameState.camera.pos.z, //
                 globalGameState.camera.horizontal_vel.x, globalGameState.camera.horizontal_vel.y,                                     //
                 movement_vector.x, movement_vector.y, movement_vector.z,                                                              //
                 globalGameState.camera.y_speed, globalGameState.camera.standing_on_solid, globalGameState.camera.angle_H );
    }
    globalGameState.camera.pos = globalGameState.camera.pos + movement_vector;
    // A grounded player has no vertical velocity: the applied Y displacement
    // is either the gravity substep clamped to ~0, or — after a step-up onto a
    // slab/snow layer — the full step height (~+0.5). Letting the latter
    // become y_speed launches the player into the air, so reset it here. The
    // position update above still uses the full movement_vector, so the player
    // correctly ends up on top of the stepped block.
    if ( globalGameState.camera.standing_on_solid ) {
        globalGameState.camera.y_speed = 0.0f;
    } else {
        globalGameState.camera.y_speed = movement_vector.y;
    }
    // If a horizontal axis was blocked by collision, zero that component of
    // the persisted velocity so we don't keep pushing into the wall.
    globalGameState.camera.horizontal_vel.x = movement_vector.x;
    globalGameState.camera.horizontal_vel.y = movement_vector.z;
    // pr_debug("Y speed:%f",globalGameState.camera.y_speed);

    // Footsteps: while grounded, accumulate the actual horizontal distance
    // covered (collision-resolved, so pushing a wall stays silent) and play
    // the underfoot block's step sound every ~1.6 blocks — about MC's
    // cadence, ~3 steps/s at walk speed. A 0.2s minimum gap keeps sprint
    // (~18 blocks/s) from machine-gunning steps, and a 0.6s idle threshold
    // makes any motion after a pause count immediately. Any air-to-ground
    // transition also plays a step, so jumping lands with a footfall.
    {
        static double step_distance = 0.0;
        static bool was_grounded = false;
        static long last_step_tick = -1000;
        const bool grounded = globalGameState.camera.standing_on_solid && !player_flying && !globalGameState.input.no_clip;
        bool step_now = false;
        if ( grounded ) {
            step_distance += glm::length( glm::dvec2( movement_vector.x, movement_vector.z ) );
            const long since_step = globalGameState.tick_number - last_step_tick;
            const bool gap_ok = since_step >= ( long )( 0.2 * UPS_RATE );
            // A small motion after a long pause still counts as a step —
            // otherwise sub-1.6-block shuffles stay silent forever. 0.45
            // blocks is far enough into the motion that a single shuffle
            // reads as one step, not two.
            const bool resumed = step_distance >= 0.45 && since_step >= ( long )( 0.6 * UPS_RATE );
            if ( gap_ok && ( !was_grounded || step_distance >= 1.6 || resumed ) ) {
                step_now = true;
                step_distance = 0.0;
                last_step_tick = globalGameState.tick_number;
            }
        } else {
            step_distance = 0.0;
        }
        was_grounded = grounded;
        if ( step_now ) {
            const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
            const glm::ivec3 under_pos = glm::ivec3( glm::round( glm::dvec3( globalGameState.camera.pos.x, feet_y - 0.05, globalGameState.camera.pos.z ) - 0.5 ) );
            const BlockState under = globalGameState.world.get_loaded_block( under_pos );
            if ( under.id != AIR && under.id != LAST_BLOCK_ID && block_definition_get_definition( under.id )->flows == 0 ) {
                audio_play_sfx_gain( audio_block_step_pool( under.id ), 0.75f );
            }
        }
    }

    // Farmland trampling and fall damage share one fall tracker: landing
    // after real downward travel (>0.5 — a jump lands ~1.3, a step-up 0)
    // converts a field underfoot back to dirt and, past the safe distance,
    // hurts the player. Flying, no-clip and swimming don't count (same
    // model as the mob sim) — water entry resets the distance.
    {
        static bool was_grounded_trample = false;
        double &fall_dist = globalGameState.vitals.fall_dist;
        const bool grounded = globalGameState.camera.standing_on_solid && !player_flying && !globalGameState.input.no_clip;
        if ( grounded ) {
            if ( !was_grounded_trample && fall_dist > 0.5 ) {
                const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
                const glm::ivec3 under_pos = glm::ivec3( glm::round( glm::dvec3( globalGameState.camera.pos.x, feet_y - 0.05, globalGameState.camera.pos.z ) - 0.5 ) );
                if ( globalGameState.world.get_loaded_block( under_pos ).id == FARMLAND ) {
                    repgame_convert_block( globalGameState, under_pos, DIRT );
                }
                // MC's rule: floor(distance) - 3 half-hearts.
                const float fall_damage = static_cast<float>( floor( fall_dist ) ) - static_cast<float>( PLAYER_FALL_SAFE_DISTANCE );
                if ( RepGame::player_damage( globalGameState, fall_damage ) ) {
                    audio_play_sfx( fall_damage >= 8.0f ? "fall_big" : "fall_small" );
                }
            }
            fall_dist = 0.0;
        } else if ( player_swimming ) {
            fall_dist = 0.0;
        } else if ( movement_vector.y < 0.0 && !player_flying && !globalGameState.input.no_clip && !player_climbing ) {
            fall_dist += -movement_vector.y;
        }
        was_grounded_trample = grounded;
    }

    // Water: a splash on entry — heavy when falling in fast, and gain
    // scaled by entry speed so wading in quietly laps instead of booming
    // like a dive — and swim strokes every ~1.5 blocks of submerged
    // movement (3D distance, so diving straight down counts). Wading keeps
    // footsteps instead. The liquid checks below use the POST-movement
    // position (player_swimming / player_wading are computed pre-movement
    // and lag one tick — that delay is audible on the splash).
    {
        static bool was_in_liquid = false;
        static bool liquid_state_known = false;
        static double swim_distance = 0.0;
        static long last_swim_tick = -1000;
        static long last_liquid_tick = -1000;
        const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
        const BlockState feet_block_now = globalGameState.world.get_loaded_block( glm::ivec3( glm::round( glm::dvec3( globalGameState.camera.pos.x, feet_y + 0.05, globalGameState.camera.pos.z ) - 0.5 ) ) );
        const BlockState body_block_now = globalGameState.world.get_loaded_block( glm::ivec3( glm::round( body_center - 0.5 ) ) );
        const bool feet_in_liquid = feet_block_now.id < LAST_BLOCK_ID && block_definition_get_definition( feet_block_now.id )->flows != 0;
        const bool body_in_liquid_now = body_block_now.id < LAST_BLOCK_ID && block_definition_get_definition( body_block_now.id )->flows != 0;
        const bool in_liquid = ( feet_in_liquid || body_in_liquid_now ) && !player_flying && !globalGameState.input.no_clip;
        // Unloaded chunks read as LAST_BLOCK_ID: skip until they load, and take
        // the first loaded sample silently so spawning underwater doesn't splash.
        const bool blocks_loaded = feet_block_now.id != LAST_BLOCK_ID && body_block_now.id != LAST_BLOCK_ID;
        if ( blocks_loaded && !liquid_state_known ) {
            liquid_state_known = true;
            was_in_liquid = in_liquid;
        }
        if ( in_liquid ) {
            if ( !was_in_liquid ) {
                // Only a real dry spell counts as a fresh entry: flow writes
                // receding/refilling the cell underfoot toggle in_liquid as
                // the shape settles, and each rewrite used to re-splash.
                const long dry_ticks = globalGameState.tick_number - last_liquid_tick;
                const double entry_speed = glm::length( movement_vector );
                if ( dry_ticks >= ( long )( 0.5 * UPS_RATE ) && entry_speed > 0.01 ) {
                    const float gain = glm::clamp( (float)( entry_speed / 0.35 ), 0.05f, 1.0f );
                    audio_play_sfx_gain( -movement_vector.y > 0.25 ? "heavy_splash" : "splash", gain );
                }
                swim_distance = 0.0;
                last_swim_tick = globalGameState.tick_number;
            } else if ( body_in_liquid_now ) {
                swim_distance += glm::length( movement_vector );
                const long since_swim = globalGameState.tick_number - last_swim_tick;
                if ( swim_distance >= 1.5 && since_swim >= ( long )( 0.3 * UPS_RATE ) ) {
                    audio_play_sfx( "swim" );
                    swim_distance = 0.0;
                    last_swim_tick = globalGameState.tick_number;
                }
            }
        } else {
            swim_distance = 0.0;
        }
        if ( blocks_loaded ) {
            was_in_liquid = in_liquid;
            if ( in_liquid ) {
                last_liquid_tick = globalGameState.tick_number;
            }
        }
    }
}

void RepGame::process_block_updates( ) {
    PROFILE_SCOPE( "block_updates" );
    // Random ticks (growth + grass) stay off in capture/fly/flat/block-world
    // runs — the same gates the mob sim uses — so captures stay
    // deterministic. They also pause while the player is flying or no-
    // clipping: traversal is what covers ground, and every natural write
    // dirties (and eventually persists) the chunk it's in — a spectator's
    // fly-through shouldn't be filing chunks no one edited.
    if ( !RepGame::capture_mode( ) && !RepGame::fly_mode( ) && !repgame_block_world( ) && !repgame_flat_world( ) && !globalGameState.input.player_flying && !globalGameState.input.no_clip ) {
        globalGameState.world.process_random_ticks( globalGameState );
    }
    globalGameState.blockUpdateQueue.processAllBlockUpdates( globalGameState, globalGameState.tick_number );
}

void RepGame::process_pressure_plates( ) {
    const glm::dvec3 &eye = globalGameState.camera.pos;
    const double half_w = PLAYER_WIDTH / 2.0;
    const double feet_y = eye.y - globalGameState.camera.eye_height;
    // Cells overlapped by the player's feet; the plate lives in the same cell
    // as the feet when standing on it (it doesn't collide, so feet rest at
    // the supporting block's top face).
    const double eps = 1e-4;
    const int y = static_cast<int>( floor( feet_y ) );
    const int x0 = static_cast<int>( floor( eye.x - half_w ) );
    const int x1 = static_cast<int>( floor( eye.x + half_w - eps ) );
    const int z0 = static_cast<int>( floor( eye.z - half_w ) );
    const int z1 = static_cast<int>( floor( eye.z + half_w - eps ) );
    for ( int x = x0; x <= x1; x++ ) {
        for ( int z = z0; z <= z1; z++ ) {
            const glm::ivec3 block_pos( x, y, z );
            const BlockState block_state = globalGameState.world.get_loaded_block( block_pos );
            if ( block_state.id == LAST_BLOCK_ID ) {
                continue;
            }
            // First time standing on this plate starts a check chain; the
            // event reschedules itself until nobody is on it. Watching is what
            // bounds this to one chain per plate.
            if ( block_definition_get_definition( block_state.id )->is_pressure_plate && globalGameState.watched_pressure_plates.insert( block_pos ).second ) {
                globalGameState.blockUpdateQueue.addBlockUpdate<PressurePlateEvent>( globalGameState.tick_number, block_pos );
            }
        }
    }
}
