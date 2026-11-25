// RepGame member functions: block interaction and placement.

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


// MC's scaffolding use-on redirect: when the clicked cell is scaffolding,
// picks the placement cell by walking a direction while the cells are
// scaffolding and stopping at the first non-scaffold cell. A side/bottom
// click walks UP to the column's top (tower-building); a top-face click
// walks in the player's horizontal facing direction — skipping the beam's
// own cells — so a top click anywhere on a beam extends its end
// (bridge-building). Sneaking bypasses this entirely: the caller places
// against the exact face hit.
bool repgame_scaffold_place_target( RepGameState &state, const glm::ivec3 &clicked, glm::ivec3 *target ) {
    World &world = state.world;
    const BlockState clicked_state = world.get_loaded_block( clicked );
    if ( clicked_state.id == LAST_BLOCK_ID || !block_definition_get_definition( clicked_state.id )->is_scaffolding ) {
        return false;
    }
    glm::ivec3 dir( 0, 1, 0 );
    if ( state.block_selection.face == FACE_TOP ) {
        // Player facing: the dominant horizontal axis of the look vector
        // (look.xz keeps the angle_H ratio even at straight-up/down pitch).
        const glm::vec3 look = state.camera.look;
        dir = fabsf( look.x ) > fabsf( look.z ) ? glm::ivec3( look.x > 0.0f ? 1 : -1, 0, 0 ) : glm::ivec3( 0, 0, look.z > 0.0f ? 1 : -1 );
    }
    glm::ivec3 end = clicked;
    BlockState at;
    do {
        end += dir;
        at = world.get_loaded_block( end );
    } while ( at.id != LAST_BLOCK_ID && block_definition_get_definition( at.id )->is_scaffolding );
    *target = end;
    return true;
}

BlockID RepGame::change_block( const int place, BlockState blockState ) {

    glm::ivec3 block;
    if ( place ) {
        // Placing a leaf item writes the player-placed (persistent) variant —
        // only generator-grown leaves decay.
        blockState.id = block_placed_leaf_id( blockState.id );
        blockState.display_id = blockState.id;
        block = globalGameState.block_selection.pos_create;
        const Block *placed_def = block_definition_get_definition( blockState.id );
        if ( placed_def->is_scaffolding && !globalGameState.input.movement.sneakPressed ) {
            glm::ivec3 redirected;
            if ( repgame_scaffold_place_target( globalGameState, globalGameState.block_selection.pos_destroy, &redirected ) ) {
                block = redirected;
            }
        }
        if ( placed_def->collides_with_player ) {
            // If the block collides with the player, make sure it's not being placed where it would collide
            if ( Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos, block, blockState, //
                                                       globalGameState.camera.player_height, globalGameState.camera.eye_height ) ) {
                return LAST_BLOCK_ID;
            }
            // A door lower also occupies the cell above — checked with the
            // partner half's state (its final hinge bit doesn't move the
            // closed panel's box).
            if ( placed_def->is_door && !placed_def->is_door_upper ) {
                BlockState upper_state = blockState;
                upper_state.id = placed_def->door_partner;
                upper_state.display_id = placed_def->door_partner;
                if ( Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos, block + glm::ivec3( 0, 1, 0 ), upper_state, //
                                                           globalGameState.camera.player_height, globalGameState.camera.eye_height ) ) {
                    return LAST_BLOCK_ID;
                }
            }
            // A bed foot also occupies the head cell one block in the
            // placer's facing direction.
            if ( placed_def->bed_partner != LAST_BLOCK_ID && !placed_def->is_bed_head ) {
                BlockState head_state = blockState;
                head_state.id = placed_def->bed_partner;
                head_state.display_id = placed_def->bed_partner;
                if ( Collision::check_collides_with_block( globalGameState.world, globalGameState.camera.pos,
                                                           block + block_bed_partner_dir( placed_def, blockState.rotation ), head_state, //
                                                           globalGameState.camera.player_height, globalGameState.camera.eye_height ) ) {
                    return LAST_BLOCK_ID;
                }
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
    schedule_block_update<PlayerBlockPlacedEvent>( globalGameState, 0, block, blockState, false );
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
    // Levers mount on walls, floors and ceilings. Side clicks keep the
    // needs.front mount yaw block_mount_rotation already returned (buttons
    // ride the same path); top/bottom clicks pick the floor/ceiling model
    // with the throw axis along the player's facing axis, like MC.
    if ( block->is_lever ) {
        const int face = globalGameState.block_selection.face;
        const float yaw = globalGameState.camera.angle_H;
        const bool x_axis = ( yaw >= 45 && yaw < 135 ) || ( yaw >= 225 && yaw < 315 );
        if ( face == FACE_TOP ) {
            rotation = static_cast<unsigned char>( x_axis ? LEVER_ROT_FLOOR_X : LEVER_ROT_FLOOR );
        } else if ( face == FACE_BOTTOM ) {
            rotation = static_cast<unsigned char>( x_axis ? LEVER_ROT_CEILING_X : LEVER_ROT_CEILING );
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
                repgame_loot_container( globalGameState, pos, contents.slots.data( ), static_cast<int>( contents.slots.size( ) ) );
            }
            // The chunk erases the map entry when the block write lands.
        }
        // Close a UI still showing the broken container (one arm per kind —
        // the UI structs stay per-kind even though the sync is generic). A
        // double chest closes when either half breaks — the survivor's
        // latch reverts to a single chest via the neighbour wake.
        switch ( def->kind ) {
            case NetContainerKind::CHEST:
                if ( globalGameState.ui.chest.open &&
                     ( globalGameState.ui.chest.pos == pos || ( globalGameState.ui.chest.paired && globalGameState.ui.chest.pos2 == pos ) ) ) {
                    RepGame::close_chest( globalGameState );
                    globalGameState.input.inventory_open = false;
                }
                break;
            case NetContainerKind::FURNACE:
                if ( globalGameState.ui.furnace.open && globalGameState.ui.furnace.pos == pos ) {
                    RepGame::close_furnace( globalGameState );
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
        // The removal is still queued, so the cell still holds the broken
        // block — read its rotation for the horizontal bed-pair check, but
        // keep the retargeted id (piston heads drop the base's item).
        BlockState broken_state = globalGameState.world.get_loaded_block( globalGameState.block_selection.pos_destroy );
        broken_state.id = previous_block;
        broken_state.display_id = previous_block;
        break_drops( globalGameState, globalGameState.block_selection.pos_destroy, broken_state, force_item );
    }
}
