#include <cstdlib>

#include "common/block_definitions.hpp"

Block *block_definitions;

void block_definitions_initilize_definitions( Texture *texture ) {
    block_definitions = static_cast<Block *>( calloc( LAST_BLOCK_ID, sizeof( Block ) ) );
    if ( block_definitions == NULL ) {
        pr_debug( "Failed to allocate block_definitions" );
        abort( );
    }
    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];
        block->id = static_cast<BlockID>( block_id );
        block->renderOrder = RenderOrder_Opaque;
        block->no_light = NO_LIGHT_NO_DRAW;
        block->casts_shadow = true;
        block->hides_self = { false, false, false }; // like connected glass so you don't see the perpendicular panes, you probably need is_seethrough too.
        block->is_seethrough = false;
        for ( bool &face : block->needs_place_on_any_solid ) {
            face = false;
        }
        block->rotate_on_placement = false;
        block->icon_is_isometric = true;
        block->is_pickable = true;
        block->collides_with_player = true;
        block->climbable = false;
        block->flows = 0;
        block->flow_range = 0;
        block->flow_slope = 0;
        block->forms_sources = false;
        block->scale = { 16, 16, 16 };
        block->offset = { 0, 0, 0 };
        block->tex_offset = { 0, 0, 0 };
        block->breaks_in_liquid = false;
        block->waterloggable = false;
        block->initial_redstone_power = 0;       // redstone block
        block->affected_by_redstone_power = true;   // dust, piston, all solid blocks
        block->transmits_redstone_power = false; // dust
        block->needs_place_on_solid_but_can_stack_on_self = false;
        block->can_float_on_water = false;
        block->can_be_placed_in = false;
        block->hardness = 1.5f;
        block->mineable = MINEABLE_NONE;
        block->connects_to_redstone_dust = false;
        block->is_redstone_dust = false;
        block->is_fence = false;
        block->is_fence_gate = false;
        block->is_stairs = false;
        block->is_slab = false;
        block->spine_shell = false;
        block->is_door = false;
        block->is_door_upper = false;
        block->door_redstone_only = false;
        block->door_partner = LAST_BLOCK_ID;
        block->is_tall_plant_upper = false;
        block->tall_plant_partner = LAST_BLOCK_ID;
        block->persistent_variant = LAST_BLOCK_ID;
        block->natural_variant = LAST_BLOCK_ID;
        block->crop_next = LAST_BLOCK_ID;
        block->is_crop = false;
        block->is_log = false;
        block->is_flower = false;
        block->emits_light = 0;
        block->is_button = false;
        block->is_pressure_plate = false;
        block->is_redstone_mechanism = false;
        block->is_tnt = false;
        block->is_inverse_tnt = false;
        block->is_solid = true;
        block->blocks_light_down = false;
        block->falls_with_gravity = false;
        block->fall_mass = 1.0f;
    }

#include "block_definitions_gen.inc"

    for ( int block_id = 0; block_id < LAST_BLOCK_ID; block_id++ ) {
        Block *block = &block_definitions[ block_id ];

        // Calculate non_full_size so the result can be cached for access at runtime.
        if ( ( block->scale.x != 16 || block->scale.y != 16 || block->scale.z != 16 ) || //
             ( block->offset.x != 0 || block->offset.y != 0 || block->offset.z != 0 ) || //
             ( block->tex_offset.x != 0 || block->tex_offset.y != 0 || block->tex_offset.z != 0 ) || //
             block->is_stairs ) {
            block->non_full_size = true;
        } else {
            block->non_full_size = false;
        }

        // Bottom-aligned partial blocks (bottom slabs, path, ...): the cell
        // holds light — lit from above/sides and passing it sideways — but
        // never passes light downward, and it caps the vertical skylight
        // column so a slab roof still shades. casts_shadow is cleared so
        // every opacity test treats the cell as light-holding; the
        // downward-block rule lives in light.cpp. Derived automatically —
        // must stay above is_solid so is_solid's non_full_size exclusion
        // is unaffected.
        if ( block->casts_shadow && !block->is_seethrough && block->scale.x == 16 && block->scale.z == 16 && //
             block->offset.x == 0 && block->offset.y == 0 && block->offset.z == 0 && block->scale.y < 16 &&   //
             !block->is_piston && !block->is_piston_head ) {
            block->blocks_light_down = true;
            block->casts_shadow = false;
        }

        // Half slabs: a colliding, bottom-aligned, half-height box over the
        // whole footprint (OAK_SLAB etc.). Derived from geometry rather than
        // the name — legacy *_DOUBLE_*_SLAB ids carry the same half box and
        // get the same top-half placement, while lookalikes stay out
        // (8px-tall crop stages don't collide; SLAB-named full cubes like
        // STONE_SLAB2 have full scale).
        block->is_slab = block->collides_with_player &&                                            //
                         block->scale.x == 16 && block->scale.y == 8 && block->scale.z == 16 &&   //
                         block->offset.x == 0 && block->offset.y == 0 && block->offset.z == 0;

        // Crop chains: every stage with a crop_next is a crop, and so is the
        // stage it points at — the chain's terminal stage is marked through
        // its predecessor.
        if ( block->crop_next < LAST_BLOCK_ID ) {
            block->is_crop = true;
            block_definitions[ block->crop_next ].is_crop = true;
        }

        // A block can hold redstone power only if it's a full opaque cube.
        // Transparent blocks (glass, leaves), non-full blocks (slabs, plates,
        // flowers), and non-colliding blocks (air, water, torches, dust) are
        // never powered. Pistons are opaque but are excluded too — a powered
        // piston must not transmit power to anything.
        block->is_solid = block->collides_with_player && !block->is_seethrough && !block->non_full_size && !block->is_piston && !block->is_piston_head;
        // if ( block->id == OAK_LEAVES ) {
        //     pr_debug( "Got" );
        // }

        block->calculated.hides_self[ FACE_TOP ] = block->hides_self.y;
        block->calculated.hides_self[ FACE_BOTTOM ] = block->hides_self.y;
        block->calculated.hides_self[ FACE_RIGHT ] = block->hides_self.x;
        block->calculated.hides_self[ FACE_FRONT ] = block->hides_self.z;
        block->calculated.hides_self[ FACE_LEFT ] = block->hides_self.x;
        block->calculated.hides_self[ FACE_BACK ] = block->hides_self.z;

        if ( block->is_seethrough ) { // glass, leaves, flowers
            block->calculated.can_mesh_x = block->scale.x == 16 ? block->hides_self.x : false;
            block->calculated.can_mesh_y = block->scale.y == 16 ? block->hides_self.y : false;
            block->calculated.can_mesh_z = block->scale.z == 16 ? block->hides_self.z : false;
        } else { // other blocks, like dirt
            block->calculated.can_mesh_x = block->scale.x == 16 ? true : false;
            block->calculated.can_mesh_y = block->scale.y == 16 ? true : false;
            block->calculated.can_mesh_z = block->scale.z == 16 ? true : false;
        }
        // The spine shell is emitted per cell — merging the core into a
        // multi-cell rect instance would give it the wrong boundary faces.
        if ( block->spine_shell ) {
            block->calculated.can_mesh_x = false;
            block->calculated.can_mesh_y = false;
            block->calculated.can_mesh_z = false;
        }

        if ( block->is_seethrough ) { // leaves, flowers, glass
            for (bool & face : block->calculated.is_seethrough_face) {
                face = true;
            }
        } else { // other blocks, like dirt, slabs

            block->calculated.is_seethrough_face[ FACE_TOP ] = ( block->scale.y + block->offset.y ) == 16 ? ( block->scale.x != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_BOTTOM ] = ( block->offset.y ) == 0 ? ( block->scale.x != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_RIGHT ] = ( block->scale.x + block->offset.x ) == 16 ? ( block->scale.y != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_FRONT ] = ( block->scale.z + block->offset.z ) == 16 ? ( block->scale.y != 16 || block->scale.x != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_LEFT ] = ( block->offset.x ) == 0 ? ( block->scale.y != 16 || block->scale.z != 16 ) : true;
            block->calculated.is_seethrough_face[ FACE_BACK ] = ( block->offset.z ) == 0 ? ( block->scale.y != 16 || block->scale.x != 16 ) : true;
        }
    }
    // Block *slab_block = &block_definitions[ STONE_BRICK_SLAB ];
    // pr_debug( "Paul SLAB:%d ", slab_block->calculated.is_seethrough_face[ FACE_TOP ] );
}

void block_definitions_free_definitions( ) {
    free( block_definitions );
}

static const char *const BLOCK_NAMES[ LAST_BLOCK_ID ] = {
#include "block_names_gen.inc"
};

const char *block_definition_get_name( BlockID id ) {
    if ( id < LAST_BLOCK_ID ) {
        return BLOCK_NAMES[ id ];
    }
    return nullptr;
}

static const char *const TEXTURE_NAMES[ TEXTURE_COUNT ] = {
#include "texture_names_gen.inc"
};

const char *texture_name( TextureID id ) {
    if ( id < TEXTURE_COUNT ) {
        return TEXTURE_NAMES[ id ];
    }
    return nullptr;
}
