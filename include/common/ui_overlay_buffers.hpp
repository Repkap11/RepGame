#pragma once

#include "common/constants.hpp"
#include "common/item.hpp"
// RepGame.hpp must be entered before ui_overlay.hpp: ui_overlay.hpp pulls
// RepGame.hpp mid-include, and only resolves its UIOverlayVertex /
// UIOverlayInstance types when the game_state -> ui_state chain reaches it
// first (see the include-order note in game_state.hpp).
#include "common/RepGame.hpp" // IWYU pragma: keep -- include order only, see above
#include "common/ui_overlay.hpp"

// Number of isometric faces rendered for a block icon (top, front, right).
#define ISOMETRIC_FACES 3

// Face indices used to look up block textures for isometric icons.
constexpr static int inventory_isometric_face[] = { FACE_TOP, FACE_FRONT, FACE_RIGHT };

// Vertex buffer for the in-hand held-block icon (lower-left corner). The
// non-isometric quad section uses adjusted coordinates so flat icons are
// positioned like the player's hand view.
#define VB_ISOMETRIC_QUAD_SIZE 16
// Isometric UVs match the chunk mesh's vd_data_solid conventions so icons
// sample textures the same way the world does: TOP u=x/v=z, FRONT u=1-x/v=y,
// RIGHT u=z/v=y (the previous top face sampled u=z/v=1-x, i.e. rotated 90°,
// which showed directional textures like piston sides on the wrong edge).
constexpr static UIOverlayVertex vb_isometric_quad_held[ VB_ISOMETRIC_QUAD_SIZE ] = {
    // Isometric
    { 0.5f, 0.625, { 1, 1 }, 1, ISO_FACE_TOP, { 1, 1, 1 } },   // 0
    { 0.0f, 0.8125f, { 0, 1 }, 1, ISO_FACE_TOP, { 0, 1, 1 } }, // 1
    { 1.0f, 0.8125f, { 1, 0 }, 1, ISO_FACE_TOP, { 1, 1, 0 } }, // 2
    { 0.5f, 1.0f, { 0, 0 }, 1, ISO_FACE_TOP, { 0, 1, 0 } },    // 3

    { 0.0f, 0.1875f, { 1, 0 }, 1, ISO_FACE_FRONT, { 0, 0, 1 } }, // 4
    { 0.0f, 0.8125f, { 1, 1 }, 1, ISO_FACE_FRONT, { 0, 1, 1 } }, // a
    { 0.5f, 0.0f, { 0, 0 }, 1, ISO_FACE_FRONT, { 1, 0, 1 } },    // 5
    { 0.5f, 0.625, { 0, 1 }, 1, ISO_FACE_FRONT, { 1, 1, 1 } },   // b

    { 0.5f, 0.0f, { 1, 0 }, 1, ISO_FACE_RIGHT, { 1, 0, 1 } },    // c
    { 0.5f, 0.625, { 1, 1 }, 1, ISO_FACE_RIGHT, { 1, 1, 1 } },   // d
    { 1.0f, 0.1875f, { 0, 0 }, 1, ISO_FACE_RIGHT, { 1, 0, 0 } }, // 6
    { 1.0f, 0.8125f, { 0, 1 }, 1, ISO_FACE_RIGHT, { 1, 1, 0 } }, // e

    // Quad (adjusted for the in-hand held-block icon). UVs are the icon's
    // transpose ((u,v) -> (1-v,1-u)): a diagonal mirror that swaps the
    // sprite's top-left and bottom-right, so the blade faces screen center
    // while the handle still reaches the hand in the lower-left.
    { 0.5f, 0.0f + 0.24, { 1, 0 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } },    //
    { 0.5f, 0.625f + 0.24, { 0, 0 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } },  //
    { 1.0f, 0.1875f + 0.24, { 1, 1 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } }, //
    { 1.0f, 0.8125f + 0.24, { 0, 1 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } }  //
};

// Vertex buffer for inventory grid icons and the dragged item. The
// non-isometric quad section uses a plain unit quad (0,0)-(1,1) so flat
// icons fill the cell exactly, matching the inventory grid.
constexpr static UIOverlayVertex vb_isometric_quad_inventory[ VB_ISOMETRIC_QUAD_SIZE ] = {
    // Isometric
    { 0.5f, 0.625, { 1, 1 }, 1, ISO_FACE_TOP, { 1, 1, 1 } },   // 0
    { 0.0f, 0.8125f, { 0, 1 }, 1, ISO_FACE_TOP, { 0, 1, 1 } }, // 1
    { 1.0f, 0.8125f, { 1, 0 }, 1, ISO_FACE_TOP, { 1, 1, 0 } }, // 2
    { 0.5f, 1.0f, { 0, 0 }, 1, ISO_FACE_TOP, { 0, 1, 0 } },    // 3

    { 0.0f, 0.1875f, { 1, 0 }, 1, ISO_FACE_FRONT, { 0, 0, 1 } }, // 4
    { 0.0f, 0.8125f, { 1, 1 }, 1, ISO_FACE_FRONT, { 0, 1, 1 } }, // a
    { 0.5f, 0.0f, { 0, 0 }, 1, ISO_FACE_FRONT, { 1, 0, 1 } },    // 5
    { 0.5f, 0.625, { 0, 1 }, 1, ISO_FACE_FRONT, { 1, 1, 1 } },   // b

    { 0.5f, 0.0f, { 1, 0 }, 1, ISO_FACE_RIGHT, { 1, 0, 1 } },    // c
    { 0.5f, 0.625, { 1, 1 }, 1, ISO_FACE_RIGHT, { 1, 1, 1 } },   // d
    { 1.0f, 0.1875f, { 0, 0 }, 1, ISO_FACE_RIGHT, { 1, 0, 0 } }, // 6
    { 1.0f, 0.8125f, { 0, 1 }, 1, ISO_FACE_RIGHT, { 1, 1, 0 } }, // e

    // Quad
    { 0, 0, { 1, 0 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } }, //
    { 0, 1, { 1, 1 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } }, //
    { 1, 0, { 0, 0 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } }, //
    { 1, 1, { 0, 1 }, 0, ISO_FACE_FRONT, { 0, 0, 0 } }  //
};

#define IB_ISOMETRIC_QUAD_SIZE 24
constexpr static unsigned int ib_isometric_quad[ IB_ISOMETRIC_QUAD_SIZE ] = {
    // Top
    // 0, 3, 1, 3, 0, 2, 4, 7, 5, 7, 4, 6, 8, 11, 9, 11, 8, 10,
    0,  3,  1, //
    3,  0,  2, //

    4,  7,  5, //
    7,  4,  6, //

    8,  11, 9,  //
    11, 8,  10, //

    13, 12, 14, //
    13, 14, 15  //
};

// Populates the block-specific fields of a UIOverlayInstance from a Block
// definition: is_isometric, tint, and id_isos. The caller is responsible for
// setting screen position, size, z-order, and is_block.
inline void set_block_icon( UIOverlayInstance &inst, const Block *block ) {
    inst.is_isometric = block->icon_is_isometric ? 1u : 0u;
    for ( int i = 0; i < 4; i++ ) {
        inst.tint[ i ] = 1.0f;
    }
    for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
        if ( !block->icon_is_isometric ) {
            // Like Reeds
            inst.id_isos[ face ] = block->icon_texture;
        } else {
            // Like grass
            inst.id_isos[ face ] = block->textures[ inventory_isometric_face[ face ] ];
        }
    }
    inst.block_scale[ 0 ] = block->scale.x;
    inst.block_scale[ 1 ] = block->scale.y;
    inst.block_scale[ 2 ] = block->scale.z;
    inst.block_offset[ 0 ] = block->offset.x;
    inst.block_offset[ 1 ] = block->offset.y;
    inst.block_offset[ 2 ] = block->offset.z;
    inst.block_tex_offset[ 0 ] = block->tex_offset.x;
    inst.block_tex_offset[ 1 ] = block->tex_offset.y;
    inst.block_tex_offset[ 2 ] = block->tex_offset.z;
}

// Populates the item-icon fields of a UIOverlayInstance from an ItemID:
// is_isometric, tint, and id_isos. Block items forward to their Block
// definition (isometric icons draw from the face textures); pure items draw
// flat with full-block scale. The caller is responsible for setting screen
// position, size, z-order, and is_block.
inline void set_item_icon( UIOverlayInstance &inst, ItemID item_id ) {
    if ( !item_is_block( item_id ) ) {
        inst.is_isometric = 0u;
        for ( int i = 0; i < 4; i++ ) {
            inst.tint[ i ] = 1.0f;
        }
        const TextureID icon = item_icon_texture( item_id );
        for ( int face = 0; face < ISOMETRIC_FACES; face++ ) {
            inst.id_isos[ face ] = icon;
        }
        inst.block_scale[ 0 ] = 16;
        inst.block_scale[ 1 ] = 16;
        inst.block_scale[ 2 ] = 16;
        inst.block_offset[ 0 ] = 0;
        inst.block_offset[ 1 ] = 0;
        inst.block_offset[ 2 ] = 0;
        inst.block_tex_offset[ 0 ] = 0;
        inst.block_tex_offset[ 1 ] = 0;
        inst.block_tex_offset[ 2 ] = 0;
        return;
    }
    set_block_icon( inst, block_definition_get_definition( static_cast<BlockID>( item_id ) ) );
}

