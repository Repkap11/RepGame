// RepGame member functions: mouse click, mining, picking, and use interactions.

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


// Left-click damage to a mob in survival (creative kills in one hit).
#define HIT_DAMAGE 4.0f

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
                                                           globalGameState.block_selection.block_hit_dist, nullptr );
    }
    if ( hit_mob >= 0 ) {
        globalGameState.input.click_delay_left = 30;
        globalGameState.block_mining.progress_ticks = 0.0f;
        globalGameState.vr.haptic_pending = true;
        audio_play_sfx( "successful_hit" );
        // Attack tier: a hit landed while falling is a crit, a sprinting
        // hit knocks back, otherwise a plain strong hit — MC's ladder.
        audio_play_sfx( !globalGameState.camera.standing_on_solid && globalGameState.camera.y_speed < -0.1 ? "attack_crit"
                        : globalGameState.input.player_sprinting                                                            ? "attack_knockback"
                                                                                                                            : "attack_strong" );
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
    // Vehicle hits pre-empt mining the same way mob hits do — recomputed
    // fresh (vehicles.tick may have removed vehicles since the selection pass).
    // Survival builds decaying damage so three quick hits break and drop
    // the vehicle's item; creative removes it outright with no drop.
    int hit_vehicle = -1;
    if ( hit_mob < 0 && globalGameState.input.mouse.buttons.left && !globalGameState.input.mouse.buttons.left_pressed_in_ui && !globalGameState.input.inventory_open && globalGameState.input.click_delay_left == 0 ) {
        hit_vehicle = globalGameState.world.vehicles.resolve_target( globalGameState.block_selection.ray_origin, globalGameState.block_selection.ray_dir, REACH_DISTANCE,
                                                             globalGameState.block_selection.block_hit_dist, nullptr );
        // The vehicle being ridden can't be hit — it fills the view.
        if ( hit_vehicle >= 0 && globalGameState.camera.riding_vehicle != 0 &&
             globalGameState.world.vehicles.id_at( hit_vehicle ) == globalGameState.camera.riding_vehicle ) {
            hit_vehicle = -1;
        }
    }
    if ( hit_vehicle >= 0 ) {
        globalGameState.input.click_delay_left = 30;
        globalGameState.block_mining.progress_ticks = 0.0f;
        globalGameState.vr.haptic_pending = true;
        audio_play_sfx( "successful_hit" );
        audio_play_sfx( "swing" );
        // Owned vehicle: local hurt (drop + remove handled inside); a mirror
        // sends ENTITY_ACTION to its owner instead.
        globalGameState.world.vehicles.hit_interact( globalGameState, hit_vehicle, globalGameState.game_mode == GameMode_Survival,
                                                  globalGameState.camera.pos );
    }
    // Right-clicking a vehicle mounts it — before item use, rail placement
    // and the boat/minecart spawn paths. Sneaking skips the mount so a
    // sneak-click still reaches the use/place paths.
    if ( globalGameState.block_selection.target_vehicle >= 0 && globalGameState.input.mouse.buttons.right &&
         !globalGameState.input.mouse.buttons.right_pressed_in_ui && !globalGameState.input.inventory_open &&
         !globalGameState.input.movement.sneakPressed && globalGameState.input.click_delay_right == 0 ) {
        const uint32_t vehicle_id = globalGameState.world.vehicles.id_at( globalGameState.block_selection.target_vehicle );
        // Multiplayer mounts go through the server (ENTITY_ACTION MOUNT +
        // pending); single player mounts immediately.
        if ( globalGameState.world.vehicles.multiplayer_mode( )
                 ? globalGameState.world.vehicles.request_mount( globalGameState, globalGameState.block_selection.target_vehicle )
                 : repgame_vehicle_mount( globalGameState, vehicle_id ) ) {
            globalGameState.input.click_delay_right = 30;
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
                                                                      globalGameState.block_selection.block_hit_dist, nullptr );
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
    const bool holding_break = hit_mob < 0 && hit_vehicle < 0 && globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left && !globalGameState.input.mouse.buttons.left_pressed_in_ui;
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
            // powered for a timeout, then released. The click delay also
            // swallows the placement attempt below.
            repgame_press_button( globalGameState, globalGameState.block_selection.pos_destroy );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-click toggles a lever instead of placing against it. Sneaking
        // skips the interaction so a block can be placed against the lever.
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_lever && !globalGameState.input.movement.sneakPressed ) {
            repgame_toggle_lever( globalGameState, globalGameState.block_selection.pos_destroy );
            audio_play_sfx( audio_block_dig_pool( target_state.id ) );
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Right-clicking a chest opens it instead of placing against it.
        // Sneaking skips the interaction so the block can be placed.
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( block_is_chest( target_state.id ) && !globalGameState.input.movement.sneakPressed ) {
            RepGame::open_chest( globalGameState, globalGameState.block_selection.pos_destroy );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-clicking a crafting table opens the 3x3 grid instead of
        // placing against it. Sneaking skips the interaction.
        if ( target_state.id == CRAFTING_TABLE && !globalGameState.input.movement.sneakPressed ) {
            globalGameState.ui.crafting.table_open = true;
            globalGameState.ui.crafting.table_pos = globalGameState.block_selection.pos_destroy;
            globalGameState.input.inventory_open = true;
            repgame_apply_ui_layout( globalGameState );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-clicking a furnace (lit or unlit) opens its smelt UI instead
        // of placing against it. Sneaking skips the interaction.
        if ( block_is_furnace( target_state.id ) && !globalGameState.input.movement.sneakPressed ) {
            RepGame::open_furnace( globalGameState, globalGameState.block_selection.pos_destroy );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-clicking a sign opens the text editor instead of placing
        // against it. Sneaking skips the interaction so a block can be
        // placed.
        if ( block_is_sign( target_state.id ) && !globalGameState.input.movement.sneakPressed ) {
            repgame_open_sign_editor( globalGameState, globalGameState.block_selection.pos_destroy );
            globalGameState.input.click_delay_right = 30;
        }
        // Right-clicking a bed sets the respawn point and, at night,
        // sleeps to dawn. Sneaking skips the interaction so a block can
        // be placed.
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_bed && !globalGameState.input.movement.sneakPressed ) {
            repgame_try_sleep( globalGameState, globalGameState.block_selection.pos_destroy );
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
            schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, globalGameState.block_selection.pos_destroy, toggled_state, true );
            audio_play_sfx( ( toggled_state.rotation & BLOCK_ROTATE_GATE_OPEN ) ? "fence_gate_open" : "fence_gate_close" );
            globalGameState.input.click_delay_right = 30;
        }
    }
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Right-clicking a repeater cycles its delay 1-4 (the delay lives in
        // display_id) instead of placing against it. Sneaking skips the
        // interaction so the block can be placed.
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_repeater && !globalGameState.input.movement.sneakPressed ) {
            schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, globalGameState.block_selection.pos_destroy,
                                                                                   repeater_cycle_delay( target_state ), true );
            audio_play_sfx( "click" );
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
                schedule_block_update<DoorEvent>( globalGameState, 0, globalGameState.block_selection.pos_destroy, DoorEvent::TOGGLE );
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
                schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, globalGameState.block_selection.pos_destroy, eaten_state, true );
            } else {
                // The last slice is eaten, not broken: no drop, no debris.
                schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, globalGameState.block_selection.pos_destroy, BLOCK_STATE_AIR, false,
                                                                                       BreakDrops::None, BreakDebris::None );
            }
            audio_play_sfx( "eat" );
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
            } else if ( target_state.id == BAMBOO || target_state.id == BAMBOO_SAPLING ) {
                // Bamboo grows two segments per use (MC's rate) — the
                // shoot converts and the column's top takes the growth.
                grown = bamboo_grow_try( globalGameState, pos, 2 );
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
                audio_play_sfx( "till" );
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
        // Right-clicking a rail while holding a minecart sets one on the
        // track — same interaction slot as the spawn egg. In multiplayer
        // spawn() announces the vehicle through ENTITY_SPAWN (and refuses
        // before WELCOME, returning 0).
        if ( globalGameState.hotbar.getSelectedItem( ) == MINECART ) {
            const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
            if ( target_state.id != LAST_BLOCK_ID && block_definition_get_definition( target_state.id )->is_rail ) {
                if ( globalGameState.world.vehicles.spawn_on_rail( globalGameState.world, globalGameState.block_selection.pos_destroy, globalGameState.camera.angle_H ) != 0 ) {
                    if ( globalGameState.game_mode == GameMode_Survival ) {
                        globalGameState.hotbar.consumeSelected( 1 );
                        globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                    }
                    globalGameState.input.click_delay_right = 30;
                }
            }
        }
        // Eye of ender on an end portal frame seats the eye; when that was
        // the ring's last empty frame the interior ignites. Same interaction
        // slot as the spawn egg / minecart.
        if ( globalGameState.hotbar.getSelectedItem( ) == ENDER_EYE && globalGameState.input.click_delay_right == 0 ) {
            const glm::ivec3 &pos = globalGameState.block_selection.pos_destroy;
            if ( end_portal_insert_eye( globalGameState, pos ) ) {
                if ( globalGameState.game_mode == GameMode_Survival ) {
                    globalGameState.hotbar.consumeSelected( 1 );
                    globalGameState.ui.overlay.set_holding_item( globalGameState.hotbar.getSelectedItem( ) );
                }
                audio_play_sfx( "pop" );
                if ( end_portal_complete_ring( globalGameState, pos ) ) {
                    audio_play_sfx( "portal_trigger" );
                }
                globalGameState.input.click_delay_right = 30;
            }
        }
        // Flint & steel: try to light a nether portal in the frame the
        // click aims into first (an obsidian or gold ring around the place
        // cell); failing that, set the place cell alight like a normal
        // fire strike. End portals don't light — eyes of ender fill them.
        if ( globalGameState.hotbar.getSelectedItem( ) == FLINT_AND_STEEL && globalGameState.input.click_delay_right == 0 ) {
            if ( portal_ignite( globalGameState, globalGameState.block_selection.pos_create ) ||
                 portal_ignite( globalGameState, globalGameState.block_selection.pos_destroy ) ) {
                audio_play_sfx( "fire_ignite" );
                globalGameState.input.click_delay_right = 30;
            } else {
                const glm::ivec3 &pos = globalGameState.block_selection.pos_create;
                const BlockState cell = globalGameState.world.get_loaded_block( pos );
                const BlockState below = globalGameState.world.get_loaded_block( pos + glm::ivec3( 0, -1, 0 ) );
                if ( cell.id != LAST_BLOCK_ID && block_definition_get_definition( cell.id )->can_be_placed_in && below.id != LAST_BLOCK_ID &&
                     block_definition_get_definition( below.id )->collides_with_player ) {
                    change_block( 1, { FIRE, BLOCK_ROTATE_0, 0, FIRE, 0 } );
                    audio_play_sfx( "fire_ignite" );
                    globalGameState.input.click_delay_right = 30;
                }
            }
        }
    }
    if ( ( globalGameState.block_selection.selectionInBounds || globalGameState.block_selection.floatSelectionInBounds ) && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && !globalGameState.input.inventory_open && globalGameState.input.click_delay_right == 0 ) {
        // Right-click with a boat item: floats on the water pick cell or
        // tops a solid block — same interaction slot as the minecart spawn
        // path. The body lives in repgame_vehicle_place so tests can
        // drive it.
        repgame_vehicle_place( globalGameState, vehicle_type_for_item( globalGameState.hotbar.getSelectedItem( ) ) );
    }
    if ( globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && !globalGameState.input.inventory_open && globalGameState.input.click_delay_right == 0 ) {
        // Right-click with the bow held releases an arrow along the look
        // direction — after the block interactions above (a chest aimed at
        // still opens), outside the placement path (the bow is a pure item
        // and fires at air too).
        if ( globalGameState.hotbar.getSelectedItem( ) == BOW && repgame_fire_arrow( globalGameState ) ) {
            globalGameState.input.click_delay_right = 30;
        }
        if ( globalGameState.input.click_delay_right == 0 && repgame_throw_ender_pearl( globalGameState ) ) {
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
                    audio_play_sfx( "crop_plant" );
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
                        schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, globalGameState.block_selection.pos_destroy, new_block_state, true );
                        globalGameState.input.click_delay_right = 30;
                        did_toggle_dust = true;
                    }
                }
            }

            if ( !did_toggle_dust ) {
                // A sign-style item covers a standing and a wall form: a
                // side-face click mounts the wall variant on the clicked
                // block, floor/ceiling clicks (or an unmountable side) keep
                // the held block — the same MC one-sign-item behavior.
                BlockID placed_id = holdingBlock;
                const glm::ivec3 mount_dir = globalGameState.block_selection.pos_create - globalGameState.block_selection.pos_destroy;
                const int wall_rotation = block_wall_variant_rotation( holdingBlockDef, mount_dir );
                if ( wall_rotation >= 0 ) {
                    const BlockID wall_id = holdingBlockDef->wall_variant;
                    const BlockState wall_state = { wall_id, static_cast<unsigned char>( wall_rotation ), 0, wall_id, 0 };
                    if ( player_placement_valid( globalGameState.world, globalGameState.block_selection.pos_create, wall_state ) ) {
                        placed_id = wall_id;
                    }
                }
                const unsigned char rotation = getPlacedRotation( placed_id );
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
                    BlockState placed_state = { placed_id, rotation, 0, placed_id, 0 };
                    if ( holdingBlockDef->is_rail ) {
                        // Resolve the track shape up front (player yaw picks
                        // the fallback axis) so the placed cell never shows
                        // a wrong shape for a tick.
                        placed_state = rail_resolve_shape( globalGameState.world, globalGameState.block_selection.pos_create, placed_state,
                                                           static_cast<int>( globalGameState.camera.angle_H ) );
                    }
                    BlockID placed_block = change_block( 1, placed_state );
                    if ( placed_block != LAST_BLOCK_ID ) {
                        audio_play_sfx( audio_block_place_pool( holdingBlock ) );
                    }
                    // Bamboo stacked on bamboo demotes the old tip's leaf
                    // crown one tier (large -> small — MC's leaves
                    // property is positional from the top).
                    if ( placed_block != LAST_BLOCK_ID && placed_id == BAMBOO ) {
                        const glm::ivec3 below_pos = globalGameState.block_selection.pos_create + glm::ivec3( 0, -1, 0 );
                        const BlockState below_state = globalGameState.world.get_loaded_block( below_pos );
                        if ( below_state.id == BAMBOO && below_state.rotation > 1 ) {
                            BlockState demoted = below_state;
                            demoted.rotation = 1;
                            schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, below_pos, demoted, true );
                        }
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
