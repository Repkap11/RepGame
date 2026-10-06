
#include <cmath>
#include <ctime>
#include <string>
#include <unistd.h>
#include <chrono>
#include <stdlib.h>

#include "common/RepGame.hpp"
#include "common/block_definitions.hpp"
#include "common/utils/map_storage.hpp"
#include "common/renderer/texture.hpp"
#include "common/world.hpp"
#include "common/renderer/vertex_buffer_layout.hpp"
#include "common/utils/ray_traversal.hpp"
#include "common/utils/collision.hpp"
#include "common/multiplayer.hpp"
#include "common/map_gen.hpp"
#include "common/block_update_events/PlayerBlockPlacedEvent.hpp"
#include "common/block_update_events/PressurePlateEvent.hpp"

static inline long long now_us( ) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
}

BlockID RepGame::change_block( const int place, BlockState blockState ) {

    glm::ivec3 block;
    if ( place ) {
        block = globalGameState.block_selection.pos_create;
        if ( block_definition_get_definition( blockState.id )->collides_with_player ) {
            // If the block collides with the player, make sure it's not being placed where it would collide
            if ( Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos, block, //
                                                       globalGameState.camera.player_height, globalGameState.camera.eye_height ) ) {
                return LAST_BLOCK_ID;
            }
        }
    } else {
        block = globalGameState.block_selection.pos_destroy;
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

void RepGame::add_to_hotbar( const bool alsoSelect, const BlockID blockId ) {
    if ( globalGameState.hotbar.getSelectedBlock( ) == blockId ) {
        // We don't support having the same block in the hotbar or inventory more than once, until there are qtys.
        return;
    }
    if ( globalGameState.hotbar.addBlock( alsoSelect, blockId ) ) {
        // The block we added might have gone into our selected slot.
        const BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
        globalGameState.ui_overlay.set_holding_block( selectedBlock );
    }
}

// Breaks the block under the cursor. In survival mode the mined block is
// added to the hotbar first, then falls back to the survival inventory.
void RepGame::break_selected_block( ) {
    // Debris is spawned by the queued PlayerBlockPlacedEvent, which covers all
    // block-removal paths (mining, support loss, water, remote edits).
    BlockID previous_block = change_block( 0, BLOCK_STATE_AIR );
    if ( globalGameState.game_mode == GameMode_Survival && previous_block != LAST_BLOCK_ID && previous_block != AIR ) {
        const Block *blockDef = block_definition_get_definition( previous_block );
        if ( blockDef->is_pickable && blockDef->renderOrder != RenderOrder_Transparent && blockDef->renderOrder != RenderOrder_Water ) {
            bool added = globalGameState.hotbar.addBlockWithQuantity( previous_block, 1 );
            if ( !added ) {
                globalGameState.survival_inventory.addBlock( previous_block, 1 );
            }
        }
    }
}

void RepGame::process_mouse_events( ) {
    if ( !globalGameState.input.inventory_open && globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.middle && globalGameState.input.click_delay_middle == 0 ) {
        BlockState blockState = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        pr_debug( "Selected block:%d rotation:%d redstone_power:%d display:%d", blockState.id, blockState.rotation, blockState.current_redstone_power, blockState.display_id );
        globalGameState.input.click_delay_middle = 30;
        if ( globalGameState.game_mode == GameMode_Survival ) {
            // In survival mode, middle-clicking picks the block onto the
            // hotbar. If a non-full stack is already on the bar, just select
            // it (nothing is pulled from the survival inventory); only move a
            // whole stack when the bar's stacks are full (or the block isn't
            // on the bar at all).
            int bar_slot = globalGameState.hotbar.findSlotWithBlock( blockState.id, true );
            if ( bar_slot >= 0 ) {
                globalGameState.hotbar.setSelectedSlot( bar_slot );
            } else if ( !globalGameState.survival_inventory.moveBlockToHotbar( blockState.id, globalGameState.hotbar, true ) ) {
                // Nothing to move: select an existing (full) stack on the bar.
                int full_slot = globalGameState.hotbar.findSlotWithBlock( blockState.id, false );
                if ( full_slot >= 0 ) {
                    globalGameState.hotbar.setSelectedSlot( full_slot );
                }
            }
            // Debug feature: also give 1 of the picked block so spamming
            // middle-click restocks the bar without mining.
            globalGameState.hotbar.addBlockWithQuantity( blockState.id, 1 );
            BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
            globalGameState.ui_overlay.set_holding_block( selectedBlock );
        } else {
            // Creative mode: always give the block.
            RepGame::add_to_hotbar( true, blockState.id );
        }
    }
    const bool holding_break = globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.left;
    if ( holding_break && globalGameState.input.click_delay_left == 0 ) {
        // Mining a block
        if ( globalGameState.game_mode == GameMode_Survival ) {
            // Survival mode: blocks take time to break. Accumulate progress
            // each tick the button is held on the same block.
            const glm::ivec3 target_pos = globalGameState.block_selection.pos_destroy;
            const BlockState target_state = globalGameState.world.get_loaded_block( target_pos );
            const Block *target_def = block_definition_get_definition( target_state.id );
            // hardness < 0 is unbreakable (bedrock/barrier); 0 breaks instantly.
            if ( target_def->hardness >= 0.0f ) {
                // Aiming at a different block (or the block at the position
                // changing) restarts progress.
                if ( globalGameState.block_mining.pos != target_pos || globalGameState.block_mining.id != target_state.id ) {
                    globalGameState.block_mining.progress_ticks = 0.0f;
                }
                globalGameState.block_mining.pos = target_pos;
                globalGameState.block_mining.id = target_state.id;
                globalGameState.block_mining.progress_ticks += 1.0f;
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
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        const BlockState target_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        if ( block_definition_get_definition( target_state.id )->is_button ) {
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
    if ( globalGameState.block_selection.selectionInBounds && globalGameState.input.mouse.buttons.right && globalGameState.input.click_delay_right == 0 ) {
        // Placing a block
        BlockID holdingBlock = globalGameState.hotbar.getSelectedBlock( );
        if ( holdingBlock != LAST_BLOCK_ID ) {
            const Block *holdingBlockDef = block_definition_get_definition( holdingBlock );
            bool did_toggle_dust = false;

            // Toggle redstone dust between cross and dot when right-clicking an existing dust block with dust in hand
            if ( holdingBlockDef->is_redstone_dust ) {
                BlockState targetBlock = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
                const Block *targetBlockDef = block_definition_get_definition( targetBlock.id );
                if ( targetBlockDef->is_redstone_dust ) {
                    BlockState new_block_state = targetBlock;
                    if ( targetBlock.display_id == REDSTONE_CROSS ) {
                        new_block_state.display_id = REDSTONE_DOT;
                    } else if ( targetBlock.display_id == REDSTONE_DOT ) {
                        new_block_state.display_id = REDSTONE_CROSS;
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
                    BlockID placed_block = change_block( 1, { holdingBlock, rotation, 0, holdingBlock, 0 } );
                    if ( placed_block != LAST_BLOCK_ID && globalGameState.game_mode == GameMode_Survival ) {
                        globalGameState.hotbar.consumeSelected( 1 );
                        BlockID newHolding = globalGameState.hotbar.getSelectedBlock( );
                        globalGameState.ui_overlay.set_holding_block( newHolding );
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

    // Decompose the polar stick input (sizeH magnitude, angleH direction
    // relative to facing) into forward/strafe fractions. angleH convention:
    // W=0, D=+90, S=180, A=-90, diagonals in between.
    const float input_angle = globalGameState.input.movement.angleH * ( M_PI / 180.0f );
    const float input_forward = globalGameState.input.movement.sizeH * cosf( input_angle );
    const float input_strafe = globalGameState.input.movement.sizeH * sinf( input_angle );

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
        // MC-style 3D steering: the forward input fraction propels along the
        // look vector, so looking up + forward swims up, looking down dives,
        // and backwards reverses it (looking up + back sinks). Strafe
        // contributes no vertical motion. Jump/sneak add vertical assist;
        // idle players drift toward a gentle sink via the drag below.
        double swim_y_target = static_cast<double>( movement_speed ) * static_cast<double>( input_forward ) * globalGameState.camera.look.y;
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

    // Build the target horizontal velocity from input. Normally movement.x/z
    // are the unit forward/right direction derived from the camera yaw; while
    // swimming the look vector's x/z are used instead so pitch steers the
    // horizontal component (looking straight up moves purely vertically).
    // sizeH is the analog input magnitude (0..1 on keyboard). The target is
    // clamped to the selected movement speed so acceleration can never push
    // us past it.
    glm::dvec2 target_vel;
    if ( player_swimming ) {
        // The forward fraction moves along look.x/z — pitch scales it, so
        // looking straight up gives no horizontal motion. The strafe
        // fraction moves along the yaw-right vector (the direction
        // angleH=+90 produces on land) and stays horizontal.
        const float yaw = globalGameState.camera.angle_H * ( M_PI / 180.0f );
        const glm::dvec2 yaw_right = glm::dvec2( cosf( yaw ), sinf( yaw ) );
        target_vel = static_cast<double>( movement_speed ) * ( static_cast<double>( input_forward ) * glm::dvec2( globalGameState.camera.look.x, globalGameState.camera.look.z ) + static_cast<double>( input_strafe ) * yaw_right );
    } else {
        const float target_vx = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.x;
        const float target_vz = movement_speed * globalGameState.input.movement.sizeH * globalGameState.camera.movement.z;
        target_vel = glm::dvec2( target_vx, target_vz );
    }

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

    static const bool walktest_log = getenv( "REPGAME_WALKTEST" ) != nullptr;
    if ( walktest_log ) {
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
}

void RepGame::process_block_updates( ) {
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

void RepGame::process_inventory_events( ) {
    if ( globalGameState.input.toggle_game_mode ) {
        globalGameState.input.toggle_game_mode = false;
        if ( globalGameState.game_mode == GameMode_Creative ) {
            globalGameState.game_mode = GameMode_Survival;
        } else {
            globalGameState.game_mode = GameMode_Creative;
        }
        pr_debug( "Game mode toggled to: %s", globalGameState.game_mode == GameMode_Creative ? "Creative" : "Survival" );
    }
    if ( globalGameState.input.drop_item ) {
        globalGameState.input.drop_item = false;
        globalGameState.hotbar.dropSelectedItem( );
        BlockID holdingBlock = globalGameState.hotbar.getSelectedBlock( );
        globalGameState.ui_overlay.set_holding_block( holdingBlock );
    }
    // A screen tap (touch platforms) is a click at absPosition while the
    // inventory is open, or a hotbar slot tap-select while it is closed.
    const bool screen_tap = globalGameState.input.screen_tap_pending;
    globalGameState.input.screen_tap_pending = false;
    if ( globalGameState.input.inventory_open ) {
        if ( ( globalGameState.input.mouse.buttons.left && globalGameState.input.mouse.buttons.left_click_handled == false ) || screen_tap ) {
            globalGameState.input.mouse.buttons.left_click_handled = true;
            if ( globalGameState.game_mode == GameMode_Creative ) {
                BlockID blockId = globalGameState.main_inventory.whichBlockClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                // pr_debug( "Clicked on %d %d blockID:%d", globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y, blockId );
                if ( blockId != LAST_BLOCK_ID ) {
                    add_to_hotbar( true, blockId );
                } else {
                    // Clicks on the visible hotbar select its slots too.
                    const int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( hotbar_slot >= 0 ) {
                        globalGameState.hotbar.setSelectedSlot( hotbar_slot );
                        const BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                        globalGameState.ui_overlay.set_holding_block( selectedBlock );
                    }
                }
            } else {
                // Survival mode
                bool shift_held = globalGameState.input.shift_held;
                if ( shift_held ) {
                    // Shift-click: move the stack to the other inventory.
                    int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( slot >= 0 ) {
                        globalGameState.survival_inventory.moveToHotbar( slot, globalGameState.hotbar, false );
                        BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                        globalGameState.ui_overlay.set_holding_block( selectedBlock );
                    } else {
                        int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            globalGameState.hotbar.moveToSurvivalInventory( hotbar_slot, globalGameState.survival_inventory );
                            BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                            globalGameState.ui_overlay.set_holding_block( selectedBlock );
                        }
                    }
                } else {
                    // Normal click: Minecraft-style pick-up / place / swap.
                    int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( slot >= 0 ) {
                        globalGameState.survival_inventory.pickupOrSwapSlot( slot, globalGameState.held_inventory_slot, globalGameState.is_holding_inventory_slot );
                    } else {
                        int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            globalGameState.hotbar.pickupOrSwapSlot( hotbar_slot, globalGameState.held_inventory_slot, globalGameState.is_holding_inventory_slot );
                            BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                            globalGameState.ui_overlay.set_holding_block( selectedBlock );
                        }
                    }
                }
            }
        }
        if ( globalGameState.input.mouse.buttons.middle && globalGameState.input.mouse.buttons.middle_click_handled == false ) {
            globalGameState.input.mouse.buttons.middle_click_handled = true;
            if ( globalGameState.game_mode == GameMode_Survival ) {
                // Survival mode: middle-clicking a survival inventory slot moves
                // the stack to the hotbar.
                int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( slot >= 0 ) {
                    globalGameState.survival_inventory.moveToHotbar( slot, globalGameState.hotbar, false );
                    BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                    globalGameState.ui_overlay.set_holding_block( selectedBlock );
                }
            }
        }
        if ( globalGameState.input.mouse.buttons.right && globalGameState.input.mouse.buttons.right_click_handled == false ) {
            globalGameState.input.mouse.buttons.right_click_handled = true;
            if ( globalGameState.game_mode == GameMode_Survival ) {
                int slot = globalGameState.survival_inventory.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                if ( globalGameState.input.shift_held ) {
                    // Shift-right-click: transfer half of the stack (rounded
                    // up) between the survival inventory and the hotbar.
                    if ( slot >= 0 ) {
                        globalGameState.survival_inventory.moveHalfToHotbar( slot, globalGameState.hotbar );
                    } else {
                        int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                        if ( hotbar_slot >= 0 ) {
                            globalGameState.hotbar.moveHalfToSurvivalInventory( hotbar_slot, globalGameState.survival_inventory );
                        }
                    }
                    BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                    globalGameState.ui_overlay.set_holding_block( selectedBlock );
                } else if ( slot >= 0 ) {
                    // Right-click picks up half a stack (rounded up), or
                    // places a single held block into the slot.
                    globalGameState.survival_inventory.rightClickSlot( slot, globalGameState.held_inventory_slot, globalGameState.is_holding_inventory_slot );
                } else {
                    int hotbar_slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
                    if ( hotbar_slot >= 0 ) {
                        globalGameState.hotbar.rightClickSlot( hotbar_slot, globalGameState.held_inventory_slot, globalGameState.is_holding_inventory_slot );
                        BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                        globalGameState.ui_overlay.set_holding_block( selectedBlock );
                    }
                }
            }
        }
    } else {
        if ( screen_tap ) {
            // Tapping a rendered hotbar slot selects it.
            const int slot = globalGameState.hotbar.inventory_renderer.whichSlotClicked( globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y );
            if ( slot >= 0 ) {
                globalGameState.hotbar.setSelectedSlot( slot );
                const BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
                globalGameState.ui_overlay.set_holding_block( selectedBlock );
            }
        }
        // Steal any left/middle click events so they don't trigger right when we open the inventory.
        globalGameState.input.mouse.buttons.left_click_handled = true;
        globalGameState.input.mouse.buttons.right_click_handled = true;
        globalGameState.input.mouse.buttons.middle_click_handled = true;
        // If we were holding an inventory item when the inventory closed, return
        // it to the survival inventory to avoid item loss.
        if ( globalGameState.is_holding_inventory_slot ) {
            int leftover = globalGameState.survival_inventory.addBlock( globalGameState.held_inventory_slot.block_id, globalGameState.held_inventory_slot.quantity );
            if ( leftover > 0 ) {
                globalGameState.hotbar.addBlockWithQuantity( globalGameState.held_inventory_slot.block_id, leftover );
            }
            globalGameState.held_inventory_slot.block_id = LAST_BLOCK_ID;
            globalGameState.held_inventory_slot.quantity = 0;
            globalGameState.is_holding_inventory_slot = false;
        }
    }
}

void RepGame::tick( ) {
    if ( globalGameState.input.exitGame ) {
        // Don't bother updating the state if the game is exiting
        return;
    }
    globalGameState.tick_number++;
    // Day/night clock; light_daylight_factor() maps it to u_Daylight.
    globalGameState.world_time++;

    // Snapshot the camera transform as it was at the end of the previous tick, so the
    // render loop can interpolate between this and the freshly-computed transform below.
    globalGameState.camera.prev_pos = globalGameState.camera.pos;
    globalGameState.camera.prev_eye_height = globalGameState.camera.eye_height;
    globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
    globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;

    globalGameState.multiplayer.process_events( globalGameState.world );

    int wheel_diff = globalGameState.input.mouse.previousPosition.wheel_counts - globalGameState.input.mouse.currentPosition.wheel_counts;
    if ( wheel_diff != 0 ) {
        if ( globalGameState.input.inventory_open && globalGameState.game_mode == GameMode_Creative ) {
            globalGameState.main_inventory.incrementSelectedPage( wheel_diff );
        } else if ( globalGameState.input.inventory_open && globalGameState.game_mode == GameMode_Survival ) {
            BlockID holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
            globalGameState.ui_overlay.set_holding_block( holdingBlock );
        } else if ( !globalGameState.input.inventory_open ) {
            BlockID holdingBlock = globalGameState.hotbar.incrementSelectedSlot( wheel_diff );
            globalGameState.ui_overlay.set_holding_block( holdingBlock );
        }
    }

    // Apply pending block updates before recomputing the selection so the
    // cursor reflects the post-destruction world. Player clicks queue events
    // tagged with the current tick, which processAllBlockUpdates defers until
    // the next tick (it skips events with tick_number >= current_tick); so
    // the destroy queued last tick is applied here, before the ray cast below.
    RepGame::process_block_updates( );

    if ( RepGame::should_lock_pointer( ) ) {
        RepGame::process_mouse_events( );
        int whichFace = 0;
        glm::dvec3 pos_with_reach = globalGameState.camera.pos + ( glm::dvec3( globalGameState.camera.look ) * static_cast<double>( REACH_DISTANCE ) );
        globalGameState.block_selection.selectionInBounds = RayTraversal::find_block_from_to( globalGameState.world, nullptr, globalGameState.camera.pos, pos_with_reach, globalGameState.block_selection.pos_destroy, &whichFace, 0, 1, 0 );

        globalGameState.block_selection.face = whichFace;
        globalGameState.block_selection.pos_create.x = globalGameState.block_selection.pos_destroy.x + ( whichFace == FACE_RIGHT ) - ( whichFace == FACE_LEFT );
        globalGameState.block_selection.pos_create.y = globalGameState.block_selection.pos_destroy.y + ( whichFace == FACE_TOP ) - ( whichFace == FACE_BOTTOM );
        globalGameState.block_selection.pos_create.z = globalGameState.block_selection.pos_destroy.z + ( whichFace == FACE_BACK ) - ( whichFace == FACE_FRONT );

        globalGameState.world.set_selected_block( globalGameState.block_selection.pos_destroy, globalGameState.block_selection.selectionInBounds );
        const float raw_dx = static_cast<float>( globalGameState.input.mouse.currentPosition.x - globalGameState.input.mouse.previousPosition.x );
        const float raw_dy = static_cast<float>( globalGameState.input.mouse.currentPosition.y - globalGameState.input.mouse.previousPosition.y );
        // Low-pass filter the per-tick look delta so that sub-pixel / slow-pan motion
        // (where integer mouse deltas alternate 0,1,0,1) becomes a steady fractional
        // velocity instead of a stuttered step. Adds a small amount of input latency.
        globalGameState.input.mouse.smoothed_dx = globalGameState.input.mouse.smoothed_dx * ( 1.0f - MOUSE_SMOOTHING ) + raw_dx * MOUSE_SMOOTHING;
        globalGameState.input.mouse.smoothed_dy = globalGameState.input.mouse.smoothed_dy * ( 1.0f - MOUSE_SMOOTHING ) + raw_dy * MOUSE_SMOOTHING;
        globalGameState.camera.angle_H += globalGameState.input.mouse.smoothed_dx * MOUSE_SENSITIVITY;
        globalGameState.camera.angle_V += globalGameState.input.mouse.smoothed_dy * MOUSE_SENSITIVITY;
        if ( globalGameState.camera.angle_H >= 360.0f ) {
            globalGameState.camera.angle_H -= 360.0f;
        }
        if ( globalGameState.camera.angle_H < 0.0f ) {
            globalGameState.camera.angle_H += 360.0f;
        }
    } else {
        // Pointer unlocked (inventory open): abandon any in-progress mining.
        globalGameState.block_mining.progress_ticks = 0.0f;
    }

    // Feed mining progress to the selection overlay so the block shows crack
    // damage while the left button is held.
    float break_progress = 0.0f;
    if ( globalGameState.block_mining.progress_ticks > 0.0f ) {
        const float break_ticks = block_definition_get_definition( globalGameState.block_mining.id )->hardness * static_cast<float>( UPS_RATE );
        break_progress = break_ticks > 0.0f ? globalGameState.block_mining.progress_ticks / break_ticks : 0.0f;
    }
    globalGameState.world.mouseSelection.set_break_progress( break_progress );

    globalGameState.input.mouse.currentPosition.x = globalGameState.screen.width / 2.0f;
    globalGameState.input.mouse.currentPosition.y = globalGameState.screen.height / 2.0f;

    static const bool walktest = getenv( "REPGAME_WALKTEST" ) != nullptr;
    if ( walktest ) {
        // REPGAME_WALKTEST_TICKS=n: quit after n ticks (timeout/SIGTERM gets
        // swallowed by SDL, so cap the test length deterministically). Go
        // through the normal exit path so cleanup() joins the terrain
        // threads — exit() here tears down libamdhip64 while a worker may be
        // inside map_gen_load_block_hip, segfaulting during shutdown.
        static const long walktest_ticks = getenv( "REPGAME_WALKTEST_TICKS" ) ? atol( getenv( "REPGAME_WALKTEST_TICKS" ) ) : 0;
        if ( walktest_ticks && globalGameState.tick_number >= walktest_ticks ) {
            globalGameState.input.exitGame = true;
        }
        // REPGAME_TELEPORT="x,y,z,angle,sprint,spin_deg_per_tick[,pitch]": teleport on
        // the first tick, then hold forward each tick (angle optionally spins).
        const char *tp = getenv( "REPGAME_TELEPORT" );
        if ( tp ) {
            float tx, ty, tz, ta, spin = 0.0f, pitch = 0.0f;
            int sprint = 0;
            if ( sscanf( tp, "%f,%f,%f,%f,%d,%f,%f", &tx, &ty, &tz, &ta, &sprint, &spin, &pitch ) >= 4 ) {
                if ( globalGameState.tick_number == 1 ) {
                    globalGameState.camera.pos = glm::dvec3( tx, ty, tz );
                    globalGameState.camera.angle_V = pitch;
                    globalGameState.camera.y_speed = 0.0f;
                    globalGameState.input.player_sprinting = sprint != 0;
                }
                globalGameState.camera.angle_H = ta + spin * globalGameState.tick_number;
            }
        }
        // REPGAME_SCREENSHOT_TICK=n: request a framebuffer screenshot on tick n
        // (screenshots land in ./screenshots/).
        static const long screenshot_tick = getenv( "REPGAME_SCREENSHOT_TICK" ) ? atol( getenv( "REPGAME_SCREENSHOT_TICK" ) ) : 0;
        if ( screenshot_tick > 0 && globalGameState.tick_number == screenshot_tick ) {
            globalGameState.input.screenshot_requested = true;
        }
        globalGameState.input.movement.sizeH = 1.0f;
        globalGameState.input.movement.angleH = 0.0f;
        globalGameState.input.movement.jumpPressed = false;
        globalGameState.input.movement.sneakPressed = false;
    }
    // Refresh camera.movement (and the view matrices) from the current angle_H
    // before consuming it: process_movement builds the player's velocity from
    // camera.movement, so it must reflect this tick's angle_H rather than the
    // previous tick's. Running camera angle first keeps the walk direction in
    // sync with the look direction.
    RepGame::process_camera_angle( );
    RepGame::process_movement( );
    RepGame::process_pressure_plates( );

    if ( globalGameState.input.click_delay_right > 0 ) {
        globalGameState.input.click_delay_right--;
    } else {
        globalGameState.input.click_delay_right = 0;
    }
    if ( globalGameState.input.click_delay_left > 0 ) {
        globalGameState.input.click_delay_left--;
    } else {
        globalGameState.input.click_delay_left = 0;
    }
    if ( globalGameState.input.click_delay_middle > 0 ) {
        globalGameState.input.click_delay_middle--;
    } else {
        globalGameState.input.click_delay_middle = 0;
    }

    globalGameState.input.mouse.previousPosition.x = globalGameState.input.mouse.currentPosition.x;
    globalGameState.input.mouse.previousPosition.y = globalGameState.input.mouse.currentPosition.y;
    globalGameState.input.mouse.previousPosition.wheel_counts = globalGameState.input.mouse.currentPosition.wheel_counts;

    RepGame::process_inventory_events( );
}

// Whether either cell a standing player would occupy at (x, y, z) is solid
// terrain — used to keep the spawn point out of overhang stone.
static inline bool spawn_cell_blocked( const int x, const int y, const int z ) {
    const BlockID feet = MapGen::gen_block_id( x, y, z );
    const BlockID head = MapGen::gen_block_id( x, y + 1, z );
    const Block *feet_def = block_definition_get_definition( feet );
    const Block *head_def = block_definition_get_definition( head );
    return feet_def->collides_with_player || head_def->collides_with_player;
}

void RepGame::initializeGameState( const char *world_name ) {
    globalGameState.input.exitGame = false;
    globalGameState.input.player_flying = false;
    globalGameState.input.inventory_open = false;
    globalGameState.input.no_clip = false;
    globalGameState.input.mouse.smoothed_dx = 0.0f;
    globalGameState.input.mouse.smoothed_dy = 0.0f;
    globalGameState.input.shift_held = false;
    globalGameState.input.screen_tap_pending = false;
    // Start a fresh world at dawn; a save may overwrite this below.
    globalGameState.world_time = 0;
    globalGameState.block_mining.pos = glm::ivec3( 0 );
    globalGameState.block_mining.id = AIR;
    globalGameState.block_mining.progress_ticks = 0.0f;
    globalGameState.watched_pressure_plates.clear( );
    globalGameState.camera.angle_H = 0.0f;
    globalGameState.camera.angle_V = 0.0f;
    globalGameState.camera.pos.x = 0.5f;
    // Overhang noise can put solid blocks above the heightmap — walk up from
    // the terrain height until the spawn cell and the one above it are clear.
    double spawn_y = ceil( MapGen::calculateTerrainHeight( 0, 0 ) );
    while ( spawn_cell_blocked( 0, (int)spawn_y, 0 ) ) {
        spawn_y += 1.0;
    }
    globalGameState.camera.pos.y = spawn_y + PLAYER_EYE_HEIGHT + 0.5f;
    globalGameState.camera.pos.z = 0.5f;
    globalGameState.camera.y_speed = 0.0f;
    globalGameState.camera.horizontal_vel = glm::dvec2( 0.0, 0.0 );
    globalGameState.camera.sneaking = 0;
    globalGameState.camera.player_height = PLAYER_HEIGHT;
    globalGameState.camera.eye_height = PLAYER_EYE_HEIGHT;
    globalGameState.camera.eye_height_render = PLAYER_EYE_HEIGHT;
    globalGameState.input.worldDrawQuality = WorldDrawQuality::MEDIUM;
    // The hotbar's rendered height as a fraction of the UI height basis.
    // Centered inventories reserve the same amount at the bottom of the
    // screen so they never cover it.
    constexpr float hotbar_height_percent = 0.1f;
    globalGameState.main_inventory.inventory_renderer.options.active_height_percent = 0.75f;
    globalGameState.main_inventory.inventory_renderer.options.max_height_percent = 0.75f;
    globalGameState.main_inventory.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.main_inventory.inventory_renderer.options.bottom_reserved_percent = hotbar_height_percent;
    globalGameState.main_inventory.inventory_renderer.options.gravity_bottom = false;
    globalGameState.main_inventory.inventory_renderer.options.shows_selection_slot = false;

    globalGameState.hotbar.inventory_renderer.options.active_height_percent = 1.0f;
    globalGameState.hotbar.inventory_renderer.options.max_height_percent = hotbar_height_percent;
    globalGameState.hotbar.inventory_renderer.options.max_width_percent = 0.75f;
    globalGameState.hotbar.inventory_renderer.options.bottom_reserved_percent = 0.0f;
    globalGameState.hotbar.inventory_renderer.options.gravity_bottom = true;
    globalGameState.hotbar.inventory_renderer.options.shows_selection_slot = true;

    globalGameState.map_storage.init( world_name );
    PlayerData saved_data;
    if ( globalGameState.map_storage.read_player_data( saved_data ) ) {
        globalGameState.camera.pos.x = saved_data.world_x;
        globalGameState.camera.pos.y = saved_data.world_y;
        globalGameState.camera.pos.z = saved_data.world_z;
        globalGameState.camera.angle_H = saved_data.angle_H;
        globalGameState.camera.angle_V = saved_data.angle_V;
        globalGameState.input.player_flying = saved_data.flying;
        globalGameState.input.no_clip = saved_data.no_clip;
        globalGameState.input.worldDrawQuality = static_cast<WorldDrawQuality>( saved_data.worldDrawQuality );
        globalGameState.hotbar.applySavedInventory( saved_data.hotbar_inventory );
        globalGameState.hotbar.setSelectedSlot( saved_data.selected_hotbar_slot );
        globalGameState.game_mode = saved_data.game_mode;
        globalGameState.survival_inventory.applySavedInventory( saved_data.survival_inventory );
        globalGameState.world_time = saved_data.world_time;
    }
    // Seed the interpolation snapshot so the first rendered frame doesn't blend from zeroes.
    globalGameState.camera.prev_pos = globalGameState.camera.pos;
    globalGameState.camera.prev_eye_height = globalGameState.camera.eye_height;
    globalGameState.camera.prev_angle_H = globalGameState.camera.angle_H;
    globalGameState.camera.prev_angle_V = globalGameState.camera.angle_V;
    BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
    globalGameState.ui_overlay.set_holding_block( selectedBlock );
}

MK_TEXTURE( textures, 384, 832, 16, 16 );

static bool globalSupportsAnisotropic;

bool RepGame::supportsAnisotropic( ) {
    return globalSupportsAnisotropic;
}

RepGameState *RepGame::init( const char *world_name, const bool connect_multi, const char *host, const bool supportsAnisotropicFiltering ) {
    globalGameState.screen.width = DEFAULT_WINDOW_WIDTH;
    globalGameState.screen.height = DEFAULT_WINDOW_HEIGHT;
    globalSupportsAnisotropic = supportsAnisotropicFiltering;

    // Start broadcasting chunk updates
    if ( connect_multi ) {
        globalGameState.multiplayer.init( host, 25566 );
        // multiplayer_init( "localhost", 25566 );
    }

    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_BACK );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glBlendEquation( GL_FUNC_ADD );

    VertexBufferLayout vbl_ui_overlay_vertex;
    // These are from UIOverlayVertex
    vbl_ui_overlay_vertex.push_float( 2 );        // UIOverlayVertex screen_x, screen_y
    vbl_ui_overlay_vertex.push_float( 2 );        // UIOverlayVertex texture
    vbl_ui_overlay_vertex.push_unsigned_int( 1 ); // UIOverlayVertex is_isometric
    vbl_ui_overlay_vertex.push_unsigned_int( 1 ); // UIOverlayVertex face_type

    VertexBufferLayout vbl_ui_overlay_instance;
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance screen_x, screen_y, screen_z
    vbl_ui_overlay_instance.push_float( 2 );        // UIOverlayInstance width, height
    vbl_ui_overlay_instance.push_unsigned_int( 1 ); // UIOverlayInstance is_block
    vbl_ui_overlay_instance.push_unsigned_int( 1 ); // UIOverlayInstance is_isometric
    vbl_ui_overlay_instance.push_float( 3 );        // UIOverlayInstance id_isos
    vbl_ui_overlay_instance.push_float( 4 );        // UIOverlayInstance tint

    globalGameState.blocksTexture.init( texture_source_textures, 0 );
    block_definitions_initilize_definitions( &globalGameState.blocksTexture );

    globalGameState.main_inventory.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, MAIN_INVENTORY_WIDTH, MAIN_INVENTORY_HEIGHT );
    globalGameState.survival_inventory.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, SURVIVAL_INVENTORY_WIDTH, SURVIVAL_INVENTORY_HEIGHT );
    globalGameState.hotbar.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance, HOTBAR_WIDTH, HOTBAR_HEIGHT );
    globalGameState.game_mode = GameMode_Creative;
    globalGameState.held_inventory_slot.block_id = LAST_BLOCK_ID;
    globalGameState.held_inventory_slot.quantity = 0;
    globalGameState.is_holding_inventory_slot = false;

    initializeGameState( world_name );

    globalGameState.world.init( globalGameState.camera.pos, globalGameState.screen.width, globalGameState.screen.height, globalGameState.map_storage );

    globalGameState.ui_overlay.init( vbl_ui_overlay_vertex, vbl_ui_overlay_instance );
    imgui_overlay_init( &globalGameState.imgui_overlay );
    globalGameState.font_renderer.init( );

    BlockID selectedBlock = globalGameState.hotbar.getSelectedBlock( );
    globalGameState.ui_overlay.set_holding_block( selectedBlock );

    int iMultiSample = 0;
    int iNumSamples = 0;
    glGetIntegerv( GL_SAMPLE_BUFFERS, &iMultiSample );
    glGetIntegerv( GL_SAMPLES, &iNumSamples );
    pr_debug( "GL_SAMPLE_BUFFERS = %d, GL_SAMPLES = %d", iMultiSample, iNumSamples );
    return &globalGameState;
}

void RepGame::set_textures( const unsigned int which_texture, unsigned char *textures, const int textures_len ) {
    Texture::set_texture_data( which_texture, textures, textures_len );
}

bool RepGame::shouldExit( ) const {
    return globalGameState.input.exitGame;
}

bool RepGame::should_lock_pointer( ) const {
    return !globalGameState.input.inventory_open;
}
void RepGame::changeSize( const int w, const int h ) {
    // pr_debug( "Screen Size Change:%dx%d", w, h );
    if ( w == 0 || h == 0 ) {
        return;
    }
    globalGameState.screen.width = static_cast<float>( w );
    globalGameState.screen.height = static_cast<float>( h );
    glViewport( 0, 0, w, h );

    globalGameState.input.mouse.currentPosition.x = w / 2;
    globalGameState.input.mouse.currentPosition.y = h / 2;

    globalGameState.input.mouse.previousPosition.x = w / 2;
    globalGameState.input.mouse.previousPosition.y = h / 2;
    globalGameState.ui_overlay.on_screen_size_change( w, h );
    globalGameState.main_inventory.onScreenSizeChange( w, h );
    globalGameState.survival_inventory.onScreenSizeChange( w, h );
    globalGameState.hotbar.onScreenSizeChange( w, h );
    globalGameState.screen.proj = glm::perspective<float>( glm::radians( CAMERA_FOV ), globalGameState.screen.width / globalGameState.screen.height, 0.1f, 800.0f );
    globalGameState.screen.ortho = glm::ortho<float>( 0.f, w, 0.f, h, -1.f, 1.f );
    globalGameState.screen.ortho_center = glm::ortho<float>( -w / 2.0f, w / 2.0f, -h / 2.0f, h / 2.0f, -1.f, 1.f );
    globalGameState.world.change_size( w, h );
}

void RepGame::clear( ) {
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
}
void RepGame::get_screen_size( int *width, int *height ) const {
    *width = globalGameState.screen.width;
    *height = globalGameState.screen.height;
}

void RepGame::draw( float alpha ) {
    static int screenshot_counter = 0;
    if ( globalGameState.input.exitGame ) {
        // Don't bother draw the state if the game is exiting
        return;
    }
    const long long t_draw_start = now_us( );
    profiling.us_render = 0;
    profiling.us_world_draw = 0;
    profiling.us_ui_draw = 0;
    profiling.num_drawable_chunks = 0;
    profiling.num_chunks_remeshed = 0;
    // Interpolate the rendered camera between the previous tick's transform (prev_*) and the
    // current tick's transform. This decouples the visual camera from the fixed-timestep
    // simulation, so panning/walking stays smooth even when the render rate exceeds UPS_RATE
    // or when per-tick input deltas (e.g. mouse motion) are non-uniform.
    if ( alpha < 0.0f ) {
        alpha = 0.0f;
    } else if ( alpha > 1.0f ) {
        alpha = 1.0f;
    }
    glm::dvec3 render_pos = glm::mix( globalGameState.camera.prev_pos, globalGameState.camera.pos, static_cast<double>( alpha ) );
    // Interpolate the feet (not the eye) so a sneak-pose change doesn't shift
    // the ground plane, then add the render-eased eye height so the camera
    // glides down/up over a few ticks instead of snapping.
    const double prev_feet_y = globalGameState.camera.prev_pos.y - globalGameState.camera.prev_eye_height;
    const double feet_y = globalGameState.camera.pos.y - globalGameState.camera.eye_height;
    render_pos.y = glm::mix( prev_feet_y, feet_y, static_cast<double>( alpha ) ) + globalGameState.camera.eye_height_render;
    const float render_angle_V = glm::mix( globalGameState.camera.prev_angle_V, globalGameState.camera.angle_V, alpha );
    // angle_H wraps around 360, so interpolate along the shortest angular path.
    float angle_diff = globalGameState.camera.angle_H - globalGameState.camera.prev_angle_H;
    while ( angle_diff > 180.0f ) {
        angle_diff -= 360.0f;
    }
    while ( angle_diff < -180.0f ) {
        angle_diff += 360.0f;
    }
    float render_angle_H = globalGameState.camera.prev_angle_H + angle_diff * alpha;
    while ( render_angle_H < 0.0f ) {
        render_angle_H += 360.0f;
    }
    while ( render_angle_H >= 360.0f ) {
        render_angle_H -= 360.0f;
    }
    glm::vec3 render_look;
    glm::mat4 render_rotation;
    glm::mat4 render_view_look;
    glm::mat4 render_view_trans;
    build_camera_view( render_angle_H, render_angle_V, render_pos, render_look, render_rotation, render_view_look, render_view_trans );

    // Floating origin: snap an integer origin to the chunk grid so all
    // coordinates fed to the GPU stay near zero regardless of how far the
    // player is from the world origin. blockCoords in the VBO are
    // integer-valued floats, so subtracting the integer u_Origin in the
    // shader is exact in float32 (both < 2^24). The view translation also
    // becomes small, eliminating the large-float cancellation that caused
    // Z-fighting between the selection outline and terrain at distance.
    const glm::ivec3 renderOrigin = glm::ivec3( glm::floor( render_pos / glm::dvec3( CHUNK_SIZE_F ) ) ) * CHUNK_SIZE_I;
    // Subtract the integer origin in double so the small rebased offset keeps
    // full precision, then cast to float for the view matrix.
    render_view_trans = glm::translate( glm::mat4( 1.0f ), glm::vec3( -( render_pos - glm::dvec3( renderOrigin ) ) ) );

    const glm::mat4 mvp_sky = globalGameState.screen.proj * render_view_look;
    // glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * globalGameState.camera.view_look;

    const glm::mat4 mvp = mvp_sky * render_view_trans;
    constexpr glm::mat4 rotation = glm::mat4( //
        1, 0, 0, 0,                           //
        0, -1, 0, 0,                          //
        0, 0, 1, 0,                           //
        0, 0, 0, 1                            //
    );

    const glm::mat4 flipped_look = render_view_look * rotation;
    // glm::mat4 flipped_look = rotation * globalGameState.camera.view_look;

    // glm::mat4 flipped_trans = rotation * globalGameState.camera.view_trans;
    // TODO globalGameState.camera.y causes a strange jump, perhaps the vars are updated in the wrong order.
    const float height_above_water = static_cast<float>( render_pos.y ) + ( 1.0f - WATER_HEIGHT );
    const float offset = 2.0f * height_above_water;
    const glm::mat4 flipped_trans = glm::translate( render_view_trans, glm::vec3( 0.0, offset, 0.0 ) );
    // glm::mat4 flipped_trans = globalGameState.camera.view_trans;

    const glm::mat4 mvp_sky_reflect = globalGameState.screen.proj * flipped_look;

    const glm::mat4 mvp_reflect = globalGameState.screen.proj * flipped_look * flipped_trans;

    globalGameState.multiplayer.process_events( globalGameState.world );
    globalGameState.multiplayer.update_players_position( glm::vec3( render_pos ), render_rotation );

#if defined( REPGAME_WASM )
    // With pthreads enabled, terrain generation runs on background workers and
    // the main thread just drains finished chunks (like native). Without
    // pthreads, generation happens inline in dequeue(), so limit_render keeps
    // the time-budgeted loop in render_chunks from blocking the frame.
#  if defined( __EMSCRIPTEN_PTHREADS__ )
    const bool limit_render = false;
#  else
    const bool limit_render = true;
#  endif
    const bool do_render = true;
#else
    const bool do_render = true;
    const bool limit_render = false;
#endif
    if ( do_render ) {
        const long long t_render_start = now_us( );
        globalGameState.world.render( globalGameState.multiplayer, render_pos, limit_render, render_rotation );
        profiling.us_render = now_us( ) - t_render_start;
        profiling.num_drawable_chunks = globalGameState.world.get_num_drawable_chunks( );
        profiling.num_chunks_remeshed = globalGameState.world.get_num_remeshed_chunks( );
    }

    { // TEMP light diagnostic
        static int light_dbg = 0;
        if ( ++light_dbg % 120 == 0 ) {
            const glm::ivec3 p = glm::ivec3( glm::floor( render_pos ) );
            const glm::ivec3 up = p + glm::ivec3( 0, 5, 0 );
            char dbg[ 256 ];
            globalGameState.world.chunkLoader.light_dbg_stats( dbg, sizeof( dbg ) );
            pr_debug( "LIGHT probe p=(%d,%d,%d) id=%d sky=%d blk=%d | up id=%d sky=%d | q=%zu | %s",
                      p.x, p.y, p.z,
                      globalGameState.world.chunkLoader.light_probe_id( p ),
                      globalGameState.world.chunkLoader.light_probe( p, LIGHT_CHANNEL_SKY ),
                      globalGameState.world.chunkLoader.light_probe( p, LIGHT_CHANNEL_BLOCK ),
                      globalGameState.world.chunkLoader.light_probe_id( up ),
                      globalGameState.world.chunkLoader.light_probe( up, LIGHT_CHANNEL_SKY ),
                      globalGameState.world.chunkLoader.light_queue_sizes( ), dbg );
        }
    }

    showErrors( );

    // glm::mat4 mvp_mirror = glm::translate( mvp, glm::vec3( 0, -10, 0 ) );
    // glm::mat4 rotation = glm::diagonal4x4( glm::vec4( 1, 1, -1, 1 ) );
    // glm::mat4 rotation = glm::rotate( glm::mat4( 1.0 ), glm::radians( 90.0f ), glm::vec3( 1, 0, 1 ) );
    // glm::mat4 mvp_reflect = mvp * rotation;
    // mvp_mirror = glm::translate( mvp_mirror, glm::vec3( 0, -10, 0 ) );
    glm::ivec3 round_block = glm::ivec3( glm::round( render_pos - 0.5 ) );
    BlockState blockInHead = globalGameState.world.get_loaded_block( round_block );
    bool headInWater = blockInHead.id == WATER;

    globalGameState.blocksTexture.bind( );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_REPEAT );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_REPEAT );

    // ~0.04..night_ambient (deep night) .. 1.0 (noon) — scales the sky-light
    // nibble in the chunk shader and dims sky/mobs in the object shader.
    // The night_ambient floor is moonlight: it lifts terrain, sky, mobs, and
    // fog together since they all scale by this same factor.
    ImGuiDebugVars &debugVars = imgui_overlay_get_imgui_debug_vars( );
    const float daylight = glm::max( light_daylight_factor( globalGameState.world_time ), debugVars.night_ambient );

    {
        const long long t_world_draw_start = now_us( );
        // inv_mvp lets the fullscreen water shader unproject pixels to the
        // rebased world frame for screen-space reflections.
        const glm::mat4 inv_mvp = glm::inverse( mvp );
        // Wrapped to an hour so shader sine functions keep float precision.
        const float time_s = static_cast<float>( fmod( now_us( ) / 1.0e6, 3600.0 ) );
        globalGameState.world.draw( globalGameState.blocksTexture, mvp, inv_mvp, mvp_reflect, mvp_sky, mvp_sky_reflect, globalGameState.input.debug_mode, !globalGameState.input.inventory_open, render_pos.y, headInWater,
                                    globalGameState.input.worldDrawQuality, render_pos, renderOrigin, time_s, daylight, debugVars.min_ambient );
        profiling.us_world_draw = now_us( ) - t_world_draw_start;
    }

    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( globalGameState.blocksTexture.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    showErrors( );
    // Clear depth and stencil before drawing the UI/ImGui overlays. The world
    // compositing pass uses stencil on the default framebuffer (for the
    // water-blur separation) and leaves stencil values behind; clearing it
    // here ensures a clean state for the overlays.
    glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

    {
        const long long t_ui_start = now_us( );
        globalGameState.ui_overlay.draw( globalGameState.main_inventory, globalGameState.survival_inventory, globalGameState.hotbar, globalGameState.game_mode, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.input, globalGameState.font_renderer, globalGameState.screen.ortho_center );
        if ( globalGameState.is_holding_inventory_slot ) {
            int block_size, cell_size;
            globalGameState.survival_inventory.inventory_renderer.getBlockMetrics( block_size, cell_size );
            globalGameState.ui_overlay.draw_held_inventory_item( globalGameState.held_inventory_slot, globalGameState.is_holding_inventory_slot, globalGameState.input.mouse.absPosition.x, globalGameState.input.mouse.absPosition.y, block_size, cell_size, globalGameState.world.renderer, globalGameState.blocksTexture, globalGameState.font_renderer, globalGameState.screen.ortho_center );
        }
        debugVars.player_pos = glm::vec3( globalGameState.camera.pos );
        debugVars.world_time = globalGameState.world_time;
        debugVars.daylight = daylight;
        globalGameState.world.chunkLoader.debug_loader_stats( &debugVars.loader );
        imgui_overlay_draw( &globalGameState.imgui_overlay, globalGameState.input );
        // The overlay's time-of-day slider writes world_time_override while
        // dragged; apply it so lighting updates without a restart.
        if ( debugVars.world_time_override >= 0 ) {
            globalGameState.world_time = debugVars.world_time_override;
            debugVars.world_time_override = -1;
        }
        // PageUp/PageDown nudges: ±1 hour per keypress (±6 with Shift),
        // accumulated on the input side. Wrap into one day cycle.
        if ( globalGameState.input.time_skip_hours != 0 ) {
            globalGameState.world_time += static_cast<long>( globalGameState.input.time_skip_hours ) * ( DAY_LENGTH_TICKS / 24 );
            globalGameState.input.time_skip_hours = 0;
            globalGameState.world_time = ( ( globalGameState.world_time % DAY_LENGTH_TICKS ) + DAY_LENGTH_TICKS ) % DAY_LENGTH_TICKS;
        }
        profiling.us_ui_draw = now_us( ) - t_ui_start;
    }
    showErrors( );

    if ( globalGameState.input.screenshot_requested ) {
        globalGameState.input.screenshot_requested = false;
        // Capture intermediate framebuffer attachments for debugging.
        std::string prefix = "screenshot_" + std::to_string( screenshot_counter++ );
        globalGameState.world.screenshot( prefix );
    }

    // REPGAME_CAPTURE_PERIODIC=n: dump all FBO attachments + display every n
    // frames regardless of anomalies — samples whatever is on screen for
    // offline corruption scans (e.g. during daytime with terrain visible).
    static const char *periodic_env = getenv( "REPGAME_CAPTURE_PERIODIC" );
    static const int capture_periodic = periodic_env ? atoi( periodic_env ) : 0;
    static int periodic_frame = 0;
    if ( capture_periodic > 0 && ( periodic_frame++ % capture_periodic ) == 0 ) {
        std::string prefix = "periodic_" + std::to_string( screenshot_counter++ );
        globalGameState.world.screenshot( prefix );
    }

    // REPGAME_CAPTURE_ANOMALY=1: per-frame display readback, scans for
    // isolated bright RGB outliers (cheap, mild pipeline stall).
    // REPGAME_CAPTURE_ANOMALY=2: additionally blit-resolves FBO attachment 0
    // and scans its alpha for isolated outliers (heavy — halves framerate —
    // but the fog-alpha signature fires on ANY backdrop, textured terrain
    // included, where bright-speck detection is blind).
    // On a hit, dump all FBO attachments + display for forensics.
#if ( SUPPORTS_FRAME_BUFFER )
    static const char *anomaly_env = getenv( "REPGAME_CAPTURE_ANOMALY" );
    static const int capture_anomaly = anomaly_env ? atoi( anomaly_env ) : 0;
    if ( capture_anomaly ) {
        GLint viewport[ 4 ];
        glGetIntegerv( GL_VIEWPORT, viewport );
        const int w = viewport[ 2 ], h = viewport[ 3 ];
        static std::vector<unsigned char> px;
        px.resize( w * h * 4 );
        // Level 1 scans the display: skip the top-left debug overlay block
        // (imgui text trips the outlier test). Level 2 scans attachment 0,
        // which contains no UI — no skip needed.
        const int skip_y = capture_anomaly >= 2 ? -1 : h - 400, skip_x = 720;
        const bool check_alpha = capture_anomaly >= 2; // display alpha is 0/255, meaningless
        if ( capture_anomaly >= 2 ) {
            static GLuint resolveFBO = 0, resolveTex = 0;
            if ( !resolveFBO ) {
                glGenFramebuffers( 1, &resolveFBO );
                glGenTextures( 1, &resolveTex );
                glBindTexture( GL_TEXTURE_2D, resolveTex );
                glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
                glBindFramebuffer( GL_FRAMEBUFFER, resolveFBO );
                glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0 );
            }
            glBindFramebuffer( GL_READ_FRAMEBUFFER, globalGameState.world.frameBuffer.id( ) );
            glReadBuffer( GL_COLOR_ATTACHMENT0 );
            glBindFramebuffer( GL_DRAW_FRAMEBUFFER, resolveFBO );
            glDrawBuffer( GL_COLOR_ATTACHMENT0 );
            const bool fbo_ok = glCheckFramebufferStatus( GL_READ_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE &&
                                glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
            if ( !fbo_ok ) {
                goto anomaly_done; // LOW mode: no FBO path, nothing to scan
            }
            glBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
            glBindFramebuffer( GL_READ_FRAMEBUFFER, resolveFBO );
            glReadBuffer( GL_COLOR_ATTACHMENT0 );
            glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data( ) );
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        } else {
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            glReadBuffer( GL_BACK );
            glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data( ) );
        }
        static int anomaly_hits = 0, frames_since_hit = 999;
        frames_since_hit++;
        for ( int y = 1; y < h - 1 && anomaly_hits < 200 && frames_since_hit > 30; y++ ) {
            for ( int x = 1; x < w - 1; x++ ) {
                if ( y > skip_y && x < skip_x ) {
                    continue;
                }
                const unsigned char *c = &px[ ( y * w + x ) * 4 ];
                int worst_diff = 1000000, worst_alpha = 1000000;
                for ( int dy = -1; dy <= 1; dy++ ) {
                    for ( int dx = -1; dx <= 1; dx++ ) {
                        if ( dx == 0 && dy == 0 ) {
                            continue;
                        }
                        const unsigned char *n = &px[ ( ( y + dy ) * w + ( x + dx ) ) * 4 ];
                        int d = abs( c[ 0 ] - n[ 0 ] );
                        d = std::max( d, abs( c[ 1 ] - n[ 1 ] ) );
                        worst_diff = std::min( worst_diff, std::max( d, abs( c[ 2 ] - n[ 2 ] ) ) );
                        worst_alpha = std::min( worst_alpha, abs( c[ 3 ] - n[ 3 ] ) );
                    }
                }
                if ( ( c[ 0 ] + c[ 1 ] + c[ 2 ] >= 200 && worst_diff > 140 ) || ( check_alpha && worst_alpha > 80 ) ) {
                    anomaly_hits++;
                    frames_since_hit = 0;
                    pr_debug( "ANOMALY pixel at (%d,%d) rgba=(%d,%d,%d,%d) min_rgb_diff=%d min_alpha_diff=%d — dumping attachments",
                              x, y, c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ], worst_diff, worst_alpha );
                    std::string prefix = "anomaly_" + std::to_string( screenshot_counter++ );
                    globalGameState.world.screenshot( prefix );
                    goto anomaly_done;
                }
            }
        }
    anomaly_done:;
    }
#endif
    profiling.us_total_draw = now_us( ) - t_draw_start;
}

void RepGame::cleanup( ) {
    static bool clean_up_done = false;
    if ( clean_up_done ) {
        return;
    }

    PlayerData saved_data;
    saved_data.world_x = globalGameState.camera.pos.x;
    saved_data.world_y = globalGameState.camera.pos.y;
    saved_data.world_z = globalGameState.camera.pos.z;
    saved_data.angle_H = globalGameState.camera.angle_H;
    saved_data.angle_V = globalGameState.camera.angle_V;
    saved_data.flying = globalGameState.input.player_flying;
    saved_data.no_clip = globalGameState.input.no_clip;
    saved_data.worldDrawQuality = static_cast<int>( globalGameState.input.worldDrawQuality );
    globalGameState.hotbar.saveInventory( saved_data.hotbar_inventory );
    saved_data.selected_hotbar_slot = globalGameState.hotbar.getSelectedSlot( );
    saved_data.game_mode = globalGameState.game_mode;
    globalGameState.survival_inventory.saveInventory( saved_data.survival_inventory );
    saved_data.world_time = globalGameState.world_time;

    globalGameState.map_storage.write_player_data( saved_data );
#if defined( REPGAME_WASM )
    EM_ASM( "FS.syncfs(false, err => {console.log(\"Sync done, its OK to close RepGame:\", err)});" );
#endif

    globalGameState.multiplayer.cleanup( );
    globalGameState.world.cleanup( globalGameState.map_storage );
    globalGameState.blocksTexture.destroy( );
    globalGameState.ui_overlay.cleanup( );
    globalGameState.font_renderer.cleanup( );
    globalGameState.main_inventory.cleanup( );
    globalGameState.survival_inventory.cleanup( );
    globalGameState.hotbar.cleanup( );
    imgui_overlay_cleanup( &globalGameState.imgui_overlay );
    block_definitions_free_definitions( );

    clean_up_done = true;
    // pr_debug( "RepGame cleanup done" );
}

GameMode RepGame::getGameMode( ) const {
    return globalGameState.game_mode;
}

Input &RepGame::getInputState( ) {
    return globalGameState.input;
}