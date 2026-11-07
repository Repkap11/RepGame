// RepGame member functions: block interaction, camera/movement input, mouse events.

#include <cmath>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/audio.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/utils/profile.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/mp_test.hpp"
#include "common/harness/run_policy.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/DoorEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"
#include "common/game/game_internal.hpp"

// Left-click damage to a mob in survival (creative kills in one hit).
#define HIT_DAMAGE 4.0f

BlockID RepGame::change_block( const int place, BlockState blockState ) {

    glm::ivec3 block;
    if ( place ) {
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
    }
    if ( place && !player_placement_valid( globalGameState.world, block, blockState ) ) {
        return LAST_BLOCK_ID;
    }
    BlockID previous_block_id = globalGameState.world.get_loaded_block( block ).id;
    // globalGameState.world.set_loaded_block( block, blockState );
    auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number, block, blockState, false );
    globalGameState.blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
    return previous_block_id;
}

unsigned char RepGame::getPlacedRotation( const BlockID blockID ) const {
    unsigned char rotation = BLOCK_ROTATE_0;
    const Block *block = block_definition_get_definition( blockID );
    if ( block->rotate_on_placement ) {
        if ( globalGameState.camera.angle_H < 45 ) {
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
    if ( block_is_chest( previous_block ) ) {
        const glm::ivec3 chest_pos = globalGameState.block_selection.pos_destroy;
        if ( globalGameState.multiplayer.is_active( ) ) {
            // The server answers the block removal with a CHEST_CONTENTS
            // snapshot to loot — contents are server-authoritative.
            globalGameState.pending_chest_loot.insert( chest_pos );
        } else {
            const ChestContents *contents = globalGameState.world.get_chest_contents( chest_pos );
            if ( contents != nullptr ) {
                repgame_loot_chest_contents( globalGameState, *contents );
            }
            // The chunk erases the map entry when the block write lands.
        }
        if ( globalGameState.ui.chest.open && globalGameState.ui.chest.pos == chest_pos ) {
            this->close_chest( );
            globalGameState.input.inventory_open = false;
        }
    }
    if ( block_is_furnace( previous_block ) ) {
        const glm::ivec3 furnace_pos = globalGameState.block_selection.pos_destroy;
        if ( globalGameState.multiplayer.is_active( ) ) {
            // The server answers the block removal with a FURNACE_CONTENTS
            // snapshot to loot — contents are server-authoritative.
            globalGameState.pending_furnace_loot.insert( furnace_pos );
        } else {
            const FurnaceContents *contents = globalGameState.world.get_furnace_contents( furnace_pos );
            if ( contents != nullptr ) {
                repgame_loot_furnace_contents( globalGameState, *contents );
            }
            // The chunk erases the map entry when the block write lands.
        }
        if ( globalGameState.ui.furnace.open && globalGameState.ui.furnace.pos == furnace_pos ) {
            this->close_furnace( );
            globalGameState.input.inventory_open = false;
        }
    }
    if ( globalGameState.game_mode == GameMode_Survival && previous_block != LAST_BLOCK_ID && previous_block != AIR ) {
        // A door's upper half drops the lower's item. Blocks with a "drops"
        // table roll it; the rest drop themselves (kept behind the pickable /
        // render-order gate). See block_roll_drops. Each rolled drop spawns
        // as an item entity at the broken cell with a small random hop.
        static uint32_t drop_rng_state = 0x9e3779b9u;
        ItemStack drops[ 8 ];
        const int num_drops = block_roll_drops( block_item_id( previous_block ), &drop_rng_state, drops, 8 );
        const glm::dvec3 drop_pos = glm::dvec3( globalGameState.block_selection.pos_destroy ) + glm::dvec3( 0.5 );
        for ( int i = 0; i < num_drops; i++ ) {
            const ItemID drop_id = drops[ i ].item_id;
            if ( item_is_block( drop_id ) ) {
                const Block *blockDef = block_definition_get_definition( static_cast<BlockID>( drop_id ) );
                if ( !blockDef->is_pickable || blockDef->renderOrder == RenderOrder_Transparent || blockDef->renderOrder == RenderOrder_Water ) {
                    continue;
                }
            }
            globalGameState.world.items.spawn( drop_id, drops[ i ].count, drop_pos, repgame_item_drop_hop( ), 10 );
        }
    }
}

// Hotbar first (stacking onto matching slots), survival inventory for the
// overflow — shared by block-break pickups and mob drops.
// Mob melee hits: a small knockback hop away from the attacker plus the hurt
// sound. There is no player health system yet — damage is only forwarded.
void RepGame::player_hurt( RepGameState &state, const glm::dvec3 &from, float damage ) {
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
}

// Throws `count` of `item` out of the player as an item entity: spawned just
// ahead of the eye, tossed along the look direction with a little lift, and
void RepGame::process_mouse_events( ) {
    if ( !globalGameState.input.inventory_open && globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle && !globalGameState.input.mouse.buttons.middle_pressed_in_ui && globalGameState.input.click_delay_middle == 0 ) {
        BlockState blockState = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        pr_debug( "Selected block:%d rotation:%d redstone_power:%d display:%d", blockState.id, blockState.rotation, blockState.current_redstone_power, blockState.display_id );
        globalGameState.input.click_delay_middle = 30;
        // A door's upper half picks the lower's item.
        const BlockID pick_id = blockState.id != LAST_BLOCK_ID ? block_item_id( blockState.id ) : LAST_BLOCK_ID;
        if ( globalGameState.game_mode == GameMode_Survival && pick_id != LAST_BLOCK_ID ) {
            // In survival mode, middle-clicking picks the block onto the
            // hotbar. If a non-full stack is already on the bar, just select
            // it (nothing is pulled from the survival inventory); only move a
            // whole stack when the bar's stacks are full (or the block isn't
            // on the bar at all).
            int bar_slot = globalGameState.hotbar.findSlotWithItem( pick_id, true );
            if ( bar_slot >= 0 ) {
                globalGameState.hotbar.setSelectedSlot( bar_slot );
            } else if ( !globalGameState.survival_inventory.moveItemToHotbar( pick_id, globalGameState.hotbar, true ) ) {
                // Nothing to move: select an existing (full) stack on the bar.
                int full_slot = globalGameState.hotbar.findSlotWithItem( pick_id, false );
                if ( full_slot >= 0 ) {
                    globalGameState.hotbar.setSelectedSlot( full_slot );
                }
            }
            // Debug feature: also give 1 of the picked block so spamming
            // middle-click restocks the bar without mining.
            globalGameState.hotbar.addItemWithQuantity( pick_id, 1 );
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
            globalGameState.multiplayer.entities.send_entity_action( act );
        } else {
            // Survival: HIT_DAMAGE per click; creative kills in one hit. A
            // survival kill hands the type's drops to the player's inventories.
            const bool killed = globalGameState.world.mobs.hurt( hit_mob, globalGameState.game_mode == GameMode_Survival ? HIT_DAMAGE : 1000.0f, globalGameState.camera.pos );
            if ( killed && globalGameState.game_mode == GameMode_Survival ) {
                spawn_mob_drops( globalGameState, globalGameState.world.mobs.def_at( hit_mob ), globalGameState.world.mobs.pos_at( hit_mob ) );
            }
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
                auto pressEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, pressed_state, true );
                globalGameState.blockUpdateQueue.addBlockUpdate( pressEvent );
            }
            // Always (re)queue the release: idempotent while unpressed, and it
            // revives a button saved while pressed (queued events don't
            // persist across loads).
            auto releaseEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number + press_ticks, globalGameState.block_selection.pos_destroy, released_state, true );
            globalGameState.blockUpdateQueue.addBlockUpdate( releaseEvent );
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
            auto toggleEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, toggled_state, true );
            globalGameState.blockUpdateQueue.addBlockUpdate( toggleEvent );
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
                auto toggleEvent = std::make_shared<DoorEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, DoorEvent::TOGGLE );
                globalGameState.blockUpdateQueue.addBlockUpdate( toggleEvent );
                globalGameState.input.click_delay_right = 30;
            }
        }
    }
    if ( ( globalGameState.block_selection.selectionInBounds || globalGameState.block_selection.floatSelectionInBounds ) && globalGameState.input.mouse.buttons.right && !globalGameState.input.mouse.buttons.right_pressed_in_ui && globalGameState.input.click_delay_right == 0 ) {
        // Placing a block — non-block items can't be placed.
        const ItemID held_item = globalGameState.hotbar.getSelectedItem( );
        const BlockID holdingBlock = item_is_block( held_item ) ? static_cast<BlockID>( held_item ) : LAST_BLOCK_ID;
        if ( holdingBlock != LAST_BLOCK_ID ) {
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
                        auto blockPlacedEvent = std::make_shared<PlayerBlockPlacedEvent>( globalGameState.tick_number, globalGameState.block_selection.pos_destroy, new_block_state, true );
                        globalGameState.blockUpdateQueue.addBlockUpdate( blockPlacedEvent );
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
                        audio_play_sfx( audio_block_dig_pool( holdingBlock ) );
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
    const bool player_swimming = !player_flying && body_in_liquid && ( liquid_above || body_center.y < static_cast<double>( body_block_pos.y ) + WATER_HEIGHT );
    // Wading: feet in a liquid but body above the surface (e.g. 1-deep water).
    const glm::ivec3 feet_block_pos = glm::ivec3( glm::round( globalGameState.camera.pos - glm::dvec3( 0.0, globalGameState.camera.eye_height - 0.05, 0.0 ) - glm::dvec3( 0.5 ) ) );
    const BlockState feet_block = globalGameState.world.get_loaded_block( feet_block_pos );
    const bool player_wading = !player_flying && !player_swimming && feet_block.id < LAST_BLOCK_ID && block_definition_get_definition( feet_block.id )->flows != 0;

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
                audio_play_sfx( audio_block_step_pool( under.id ) );
            }
        }
    }

    // Water: a splash on entry — heavy when falling in fast — and swim
    // strokes every ~1.5 blocks of submerged movement (3D distance, so
    // diving straight down counts). Wading keeps footsteps instead. The
    // liquid checks below use the POST-movement position (player_swimming /
    // player_wading are computed pre-movement and lag one tick — that delay
    // is audible on the splash).
    {
        static bool was_in_liquid = false;
        static bool liquid_state_known = false;
        static double swim_distance = 0.0;
        static long last_swim_tick = -1000;
        const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
        const glm::dvec3 body_center( globalGameState.camera.pos.x, feet_y + PLAYER_HEIGHT / 2.0, globalGameState.camera.pos.z );
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
                audio_play_sfx( globalGameState.camera.y_speed < -0.25 ? "heavy_splash" : "splash" );
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
        }
    }
}

void RepGame::process_block_updates( ) {
    PROFILE_SCOPE( "block_updates" );
    // for ( size_t i = 0; i < 20; i++ ) {
    //     world_process_random_ticks( &globalGameState.world );
    // }
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
                auto plateEvent = std::make_shared<PressurePlateEvent>( globalGameState.tick_number, block_pos );
                globalGameState.blockUpdateQueue.addBlockUpdate( plateEvent );
            }
        }
    }
}
