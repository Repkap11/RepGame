#pragma once

#include <glm/glm.hpp>

#include "block_definitions.hpp"
#include "mesh_gen_shared.hpp" // canonical face/piston lookup tables

// See world.cpp for vbl
typedef struct {
    float x;
    float y;
    float z;

    float tex_coord_x; // Float just because it needs to be in shader, and we can avoid the gpu side cast
    float tex_coord_y;

    unsigned int which_face;
    unsigned int corner_shift;

} CubeFace;

// Order must match order of FACE_
// Two triangles per face: the per-vertex AO that needed a center vertex is
// gone, so faces are plain quads. Winding preserved from the old center fans.
static unsigned int ib_data_solid[] = {
    22, 18, 19, // Top
    22, 19, 23, //

    17, 21, 20, // Bottom
    17, 20, 16, //

    14, 13, 9,  // Right
    14, 9,  10, //

    7, 4, 5, // Front
    7, 5, 6, //

    11, 8,  12, // Left
    11, 12, 15, //

    2, 1, 0, // Back
    2, 0, 3, //
};
#define IB_SOLID_SIZE ( 3 * 2 * 6 )

static unsigned int ib_data_face[] = {
    2, 1, 4, //
    4, 1, 0, // Back
    0, 3, 4, //
    4, 3, 2, //
};
#define IB_FACE_SIZE ( 3 * 4 )

// Quad offsets into ib_data_water: the four side quads and the bottom quad
// come first — both windings each, always appended (per-face visibility is
// carried by the instance's packed_lighting, NO_LIGHT_NO_DRAW clips the
// quad in the shader) — then the two top-quad windings, still selected by
// the camera mask like before.
#define IB_POSITION_WATER_SIDES 0
#define IB_POSITION_WATER_TOP 10
#define IB_POSITION_WATER_BOTTOM 11
static unsigned int ib_data_water[] = {
    // Side quads, both windings: RIGHT, FRONT, LEFT, BACK.
    7, 6, 4,   7, 4, 5,   7, 5, 4,   7, 4, 6,   // Right
    9, 8, 10,  9, 10, 11, 9, 11, 10, 9, 10, 8,  // Front
    15, 14, 12, 15, 12, 13, 15, 13, 12, 15, 12, 14, // Left
    17, 16, 18, 17, 18, 19, 17, 19, 18, 17, 18, 16, // Back

    21, 23, 22, // Bottom
    21, 22, 20, //
    21, 20, 22, // Bottom, from the top
    21, 22, 23, //

    2, 0, 1, // Top from the top
    2, 1, 3, //

    2, 3, 1, // Top, from the bottom
    2, 1, 0, //
};
#define IB_WATER_SIZE ( 3 * 2 * 12 )

#define WATER_HEIGHT ( 0.875f )

// Fluid vertex template: a full cube whose top edge marks "surface height".
// For BLOCK_COORDS_FLUID instances the vertex shader lowers every y=1.0
// vertex to the per-corner fluid height (and slopes the top quad), so side
// quads run bottom y=0 to top y=1.0 and the top quad sits at y=1.0 instead
// of the old fixed WATER_HEIGHT. Side tex v = cube y so the flow strip can
// compress on partial faces; top u = x, v = z so the shader can rotate the
// flow texture toward the flow direction.
static CubeFace vd_data_water[] = {
    // Top quad (verts 0-3), u = x, v = z.
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_TOP, 0 }, // 0
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_TOP, 0 }, // 1

    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_TOP, 0 }, // 2
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_TOP, 0 }, // 3

    // Side quads (verts 4-19), v = cube y. RIGHT, FRONT, LEFT, BACK.
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_RIGHT, 0 }, // 4
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_RIGHT, 0 }, // 5
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_RIGHT, 0 }, // 6
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_RIGHT, 0 }, // 7

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_FRONT, 0 }, // 8
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_FRONT, 0 }, // 9
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_FRONT, 0 }, // 10
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_FRONT, 0 }, // 11

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_LEFT, 0 }, // 12
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_LEFT, 0 }, // 13
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_LEFT, 0 }, // 14
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_LEFT, 0 }, // 15

    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_BACK, 0 }, // 16
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_BACK, 0 }, // 17
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_BACK, 0 }, // 18
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_BACK, 0 }, // 19

    // Bottom quad (verts 20-23), u = x, v = 1 - z like vd_data_solid.
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_BOTTOM, 0 }, // 20
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_BOTTOM, 0 }, // 21
    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_BOTTOM, 0 }, // 22
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_BOTTOM, 0 }, // 23
};
#define VB_DATA_SIZE_WATER ( 4 * 6 )

static CubeFace vd_data_solid[] = {
    // x=right/left, y=top/bottom, z=front/back : 1/0
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_BACK, CORNER_OFFSET_bbl }, // 0
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_BACK, CORNER_OFFSET_bbr }, // 1
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_BACK, CORNER_OFFSET_tbr }, // 2
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_BACK, CORNER_OFFSET_tbl }, // 3

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_FRONT, CORNER_OFFSET_bfl }, // 4
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_FRONT, CORNER_OFFSET_bfr }, // 5
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_FRONT, CORNER_OFFSET_tfr }, // 6
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_FRONT, CORNER_OFFSET_tfl }, // 7

    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_LEFT, CORNER_OFFSET_bbl },  // 8
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_RIGHT, CORNER_OFFSET_bbr }, // 9
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_RIGHT, CORNER_OFFSET_tbr }, // 10
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_LEFT, CORNER_OFFSET_tbl },  // 11

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_LEFT, CORNER_OFFSET_bfl },  // 12
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_RIGHT, CORNER_OFFSET_bfr }, // 13
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_RIGHT, CORNER_OFFSET_tfr }, // 14
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_LEFT, CORNER_OFFSET_tfl },  // 15

    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !1, 1, FACE_BOTTOM, CORNER_OFFSET_bbl }, // 16
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ !0, 1, FACE_BOTTOM, CORNER_OFFSET_bbr }, // 17
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !0, 0, FACE_TOP, CORNER_OFFSET_tbr },    // 18
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ !1, 0, FACE_TOP, CORNER_OFFSET_tbl },    // 19

    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !1, 0, FACE_BOTTOM, CORNER_OFFSET_bfl }, // 20
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ !0, 0, FACE_BOTTOM, CORNER_OFFSET_bfr }, // 21
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !0, 1, FACE_TOP, CORNER_OFFSET_tfr },    // 22
    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ !1, 1, FACE_TOP, CORNER_OFFSET_tfl },    // 23

    // Vertices for the two crossed "flower" quads (ib_data_flowers). The
    // quads' geometric crossing is always the centre of each diagonal, so
    // along their intersection both quads interpolate the identical texture
    // column (u=0.5, the texel 7|8 boundary for a 14-texel window). Where
    // that column is transparent both fragments discard at the same pixel
    // and the block behind peeks through the crossing. Shifting both quads'
    // tex_x window by half a texel moves the crossing sample onto the centre
    // of texel 7 — a texel-centre landing is required: it picks that texel
    // under NEAREST and gets no contribution from the neighbouring column
    // under LINEAR mipmapping. Flower textures keep the stem/blade at the
    // texture centre, so texel 7 is opaque at the crossing rows for both
    // quads, and the two quads' stems stay aligned at the seam.
    // Each quad carries a single face label so the whole plane samples one
    // texture slot: the first diagonal (verts 24-27) reads textures[FACE_BACK],
    // the second (verts 28-31) textures[FACE_FRONT]. Defs where the two
    // planes need different art (chains — the second strip is offset so the
    // links interlock) map them through the back/front face keys.
    { 0.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 0, FACE_BACK, CORNER_OFFSET_bbl },   // 24
    { 1.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 0, FACE_BACK, CORNER_OFFSET_bfr },   // 25
    { 1.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 1, FACE_BACK, CORNER_OFFSET_tfr },   // 26
    { 0.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 1, FACE_BACK, CORNER_OFFSET_tbl },   // 27

    { 0.0f, 1.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 1, FACE_FRONT, CORNER_OFFSET_tfl },  // 28
    { 1.0f, 0.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 0, FACE_FRONT, CORNER_OFFSET_bbr },  // 29
    { 0.0f, 0.0f, 1.0f, /*Coords  Texture coords*/ 15.5f / 16.0f, 0, FACE_FRONT, CORNER_OFFSET_bfl },  // 30
    { 1.0f, 1.0f, 0.0f, /*Coords  Texture coords*/ -0.5f / 16.0f, 1, FACE_FRONT, CORNER_OFFSET_tbr },  // 31
};
#define VB_DATA_SIZE_SOLID ( 4 * 6 + 8 )

// FACE_*, FACE_DIR_*_OFFSETS, FACE_ROTATE_90, OPPOSITE_FACE, PISTON_ROTATE_*
// and the piston head/covered face tables now live in mesh_gen_shared.hpp —
// the single source every meshing backend (CPU, HIP, WebGPU) consumes.

// World face the head end of a piston points at for a given rotation.
static inline int piston_head_face( unsigned char rotation ) {
    return MESH_PISTON_HEAD_FACES[ rotation < 6 ? rotation : 0 ];
}

// Unit offset from a piston base toward where its head extends.
static inline glm::ivec3 piston_facing_dir( unsigned char rotation ) {
    const int face = piston_head_face( rotation );
    return glm::ivec3( FACE_DIR_X_OFFSETS[ face ], FACE_DIR_Y_OFFSETS[ face ], FACE_DIR_Z_OFFSETS[ face ] );
}

// Pixel-space (0-16) box of a piston head plate for each rotation. The mesh
// shader already yaws scale/offset for rotations 0-3, so block_adjust only
// needs this for 4/5; collision and ray code consume all six.
static inline void piston_head_shape( unsigned char rotation, short scale[ 3 ], short offset[ 3 ] ) {
    scale[ 0 ] = 16;
    scale[ 1 ] = 16;
    scale[ 2 ] = 16;
    offset[ 0 ] = 0;
    offset[ 1 ] = 0;
    offset[ 2 ] = 0;
    const int face = piston_head_face( rotation );
    // The plate is 4 pixels thick against the head-side face of the cell.
    if ( face == FACE_TOP ) {
        scale[ 1 ] = 4;
        offset[ 1 ] = 12;
    } else if ( face == FACE_BOTTOM ) {
        scale[ 1 ] = 4;
    } else if ( face == FACE_RIGHT ) {
        scale[ 0 ] = 4;
        offset[ 0 ] = 12;
    } else if ( face == FACE_LEFT ) {
        scale[ 0 ] = 4;
    } else if ( face == FACE_FRONT ) {
        scale[ 2 ] = 4;
        offset[ 2 ] = 12;
    } else { // FACE_BACK
        scale[ 2 ] = 4;
    }
}

// Fills the BlockObjectInstance shape vectors (pixels 0-16, matching
// Block::scale/offset/tex_offset) for a non-full-cube block drawn through
// block_object_vertex.glsl. item_centered recentres the box on the item
// entity's spin axis — x/z centred on (16-scale)/2, y at 0 so a slab rests
// on the ground — instead of the block's in-world offset; false uses the
// literal Block::offset so a falling body matches the block it lands as.
static inline void block_object_shape( const Block *b, const bool item_centered, glm::vec3 &scale, glm::vec3 &offset, glm::vec3 &tex_offset ) {
    scale = glm::vec3( static_cast<float>( b->scale.x ), static_cast<float>( b->scale.y ), static_cast<float>( b->scale.z ) );
    if ( item_centered ) {
        offset = glm::vec3( ( 16.0f - scale.x ) * 0.5f, 0.0f, ( 16.0f - scale.z ) * 0.5f );
    } else {
        offset = glm::vec3( static_cast<float>( b->offset.x ), static_cast<float>( b->offset.y ), static_cast<float>( b->offset.z ) );
    }
    tex_offset = glm::vec3( static_cast<float>( b->tex_offset.x ), static_cast<float>( b->tex_offset.y ), static_cast<float>( b->tex_offset.z ) );
}

// The cell-boundary face of an extended piston base that still fully covers
// its boundary: the one opposite the head end. Given as a def-space face index
// (the rotated_face/opposing_face space the meshers' coverage checks use):
// horizontal bases always cover their FACE_BACK slot, rot4 covers the world
// bottom (yaw table maps it to def BOTTOM), rot5 covers the world top.
static inline int piston_base_covered_face( unsigned char rotation ) {
    return MESH_PISTON_COVERED[ rotation < 6 ? rotation : 0 ];
}

// Pixel-space (0-16) box of an extended piston base: the full cube minus the
// 4-pixel slab at the head end, recessing the throat the arm slides through.
// Collision and ray code consume all six rotations; the mesh shader yaws
// scale/offset for rotations 0-3 so block_adjust only needs z for those.
static inline void piston_base_shape( unsigned char rotation, short scale[ 3 ], short offset[ 3 ] ) {
    scale[ 0 ] = 16;
    scale[ 1 ] = 16;
    scale[ 2 ] = 16;
    offset[ 0 ] = 0;
    offset[ 1 ] = 0;
    offset[ 2 ] = 0;
    const int face = piston_head_face( rotation );
    if ( face == FACE_TOP ) {
        scale[ 1 ] = 12;
    } else if ( face == FACE_BOTTOM ) {
        scale[ 1 ] = 12;
        offset[ 1 ] = 4;
    } else if ( face == FACE_RIGHT ) {
        scale[ 0 ] = 12;
    } else if ( face == FACE_LEFT ) {
        scale[ 0 ] = 12;
        offset[ 0 ] = 4;
    } else if ( face == FACE_FRONT ) {
        scale[ 2 ] = 12;
    } else { // FACE_BACK
        scale[ 2 ] = 12;
        offset[ 2 ] = 4;
    }
}

// Pixel-space (0-16) pick/bounding box of a torch: 4px wide on x/z instead of
// the 2px drawn stick so torches are easier to hit. Both the ray test
// (ray_traversal.cpp) and the selection outline (mouse_selection.cpp) use it;
// the drawn mesh and debris stay on the 2px stick. Wall mounts (rotation 4-7)
// hug their wall with the same mapping chunk.cpp applies.
static inline void torch_pick_shape( const Block *b, unsigned char rotation, short scale[ 3 ], short offset[ 3 ] ) {
    scale[ 0 ] = 4;
    scale[ 1 ] = b->scale.y;
    scale[ 2 ] = 4;
    offset[ 0 ] = 6;
    offset[ 1 ] = b->offset.y;
    offset[ 2 ] = 6;
    if ( rotation == 4 ) {
        offset[ 0 ] = 0;
    } else if ( rotation == 5 ) {
        offset[ 2 ] = 16 - 4;
    } else if ( rotation == 6 ) {
        offset[ 0 ] = 16 - 4;
    } else if ( rotation == 7 ) {
        offset[ 2 ] = 0;
    }
}

#define PIXEL_TO_FLOAT( pixel ) ( ( ( float )( pixel ) ) / ( 16.0f ) )

// Wall-mounted blocks (ladders, buttons — needs_place_on_any_solid on a
// side face): the rotation that attaches the block to the face the ray hit.
// mount_dir is geometric — clicked cell to new cell — which sidesteps the
// ray traversal's flipped z-face convention (RayTraversal::placement_cell).
// The world face F whose rotated def-space face is required is where
// block_supports_present looks for the support (pos - FACE_DIR_OFFSETS[F]);
// this returns the yaw mapping some required def face onto the mount side,
// or -1 when the mount direction isn't horizontal or has no required face —
// callers then fall back to yaw.
static inline int block_mount_rotation( const Block *block, const glm::ivec3 &mount_dir ) {
    for ( int face = FACE_RIGHT; face <= FACE_BACK; face++ ) {
        if ( glm::ivec3( FACE_DIR_X_OFFSETS[ face ], FACE_DIR_Y_OFFSETS[ face ], FACE_DIR_Z_OFFSETS[ face ] ) != mount_dir ) {
            continue;
        }
        int def_face = face;
        for ( int r = 0; r < 4; r++, def_face = FACE_ROTATE_90[ def_face ] ) {
            if ( block->needs_place_on_any_solid[ def_face ] ) {
                return r;
            }
        }
        return -1;
    }
    return -1;
}

// ---- Fences and fence gates -------------------------------------------------
// One sub-box of a fence/gate model, in pixel units (0-16). skip_faces is a
// bit per FACE_* in definition space — those faces get NO_LIGHT_NO_DRAW in
// the emitted instance (the mesher turns the bits by the box's yaw, since
// packed_lighting indexes the world-space quad each face lands on).
typedef struct {
    signed char scale[ 3 ];
    signed char offset[ 3 ];
    unsigned char skip_faces;
} BlockBox;

// The pick/collision boxes a piston cell actually renders (chunk.cpp emits
// the same pair): a head is the plate plus the 4x4 arm shaft spanning the
// 12px back to the cell's base end; an extended base is the recessed body
// plus the 4x4x4 arm stub seated in its open throat. All world-space for
// the six rotations; skip_faces is unused (pick/outline ignore it).
#define PISTON_MAX_BOXES 2
int piston_head_boxes( unsigned char rotation, BlockBox out[ PISTON_MAX_BOXES ] );
int piston_base_boxes( unsigned char rotation, BlockBox out[ PISTON_MAX_BOXES ] );

// Fence connection mask bits (the four horizontal directions).
#define FENCE_CONNECT_POS_X 1u
#define FENCE_CONNECT_NEG_X 2u
#define FENCE_CONNECT_POS_Z 4u
#define FENCE_CONNECT_NEG_Z 8u
#define FENCE_MAX_BOXES 5
#define FENCE_GATE_MAX_BOXES 8
// The largest multi-box item icon (the split fence-gate leaf's 7 boxes).
// Every per-box icon path — inventory slots, the dragged item, held and
// dropped item entities — sizes its instance array by this.
#define ICON_MAX_BOXES 7
// Fence gates store open/closed in rotation bit 2; the facing is rotation & 3
// and a gate spans world x when ( rotation & 1 ) == 0.
#define BLOCK_ROTATE_GATE_OPEN 4

// Whether a fence connects to a horizontal neighbor: same-family fences
// (nether brick only to nether brick), gates whose span axis matches the
// connection direction, and full solid blocks.
bool fence_connects_to( const Block *fence, BlockID neighbor_id, unsigned char neighbor_rotation, bool along_x );
// Fill `out` with the fence's boxes for a connection mask; box 0 is the post.
// Returns the box count.
int fence_boxes( unsigned connections, BlockBox out[ FENCE_MAX_BOXES ] );
// Fill `out` with the fence's pick/selection boxes: the rendered boxes
// with each direction's two rails merged into one bar, so a ray can't
// slip through the gap between them. Returns the box count.
int fence_pick_boxes( unsigned connections, BlockBox out[ FENCE_MAX_BOXES ] );
// Fill `out` with the fence's collision boxes: the post plus a bar per
// connected axis, all 24px tall (1.5 blocks, like Minecraft) so fences
// can't be jumped. Returns the box count.
int fence_collision_boxes( unsigned connections, BlockBox out[ FENCE_MAX_BOXES ] );
// Icon/pickup shape for a fence item: a lone post reads as a stick, so the
// item shows a connected segment spanning z, with each rail split where it
// crosses the post. Icon boxes draw back-to-front (one depth per box,
// later = nearer): the rail halves behind the post emit first and the
// halves in front last, so the rails read on top of the post's near face
// while the far ends tuck behind it. Returns 5 boxes.
int fence_icon_boxes( BlockBox out[ ICON_MAX_BOXES ] );
// Fill `out` with the gate's definition-space (rotation 0, spans x) boxes;
// the shader turns them with face_shift = rot & 3. Closed-leaf rails are
// pre-split where the center upright crosses them — an unsplit rail's z
// faces would sit coplanar with the upright's and z-fight. Returns the box
// count.
int fence_gate_boxes( bool open, BlockBox out[ FENCE_GATE_MAX_BOXES ] );
// Right-click toggle: closing clears bit 2; opening sets it, retargeting the
// facing to the player's yaw when it shares the gate's axis (so the gate
// opens away from the player).
unsigned char fence_gate_toggle_rotation( unsigned char rotation, unsigned char player_yaw_rot );
// Icon/pickup shape for a gate item: the closed leaf yawed 90 to span z,
// with the painter's order faking depth like the fence's split rails —
// the leaf's rails come pre-split at the upright, so the halves behind
// emit before it and the halves in front after. The far post emits first
// so the leaf reads over it, the near post last to cover the rail end cap
// inside it. Returns 7 boxes.
int fence_gate_icon_boxes( BlockBox out[ ICON_MAX_BOXES ] );

// ---- Stairs -----------------------------------------------------------------
// Stairs store the facing in rotation bits 0-1 — the yaw index doubles as the
// world direction index (0=-z, 1=+x, 2=+z, 3=-x, matching getPlacedRotation's
// buckets) — and the top/upside-down half in bit 2. The straight/inner/outer
// corner shape is never stored: it is derived live from the four horizontal
// neighbors (MC's getStairsShape) each mesh/collision/ray pass, so placing or
// breaking a stair re-shapes its neighbors through their own derives.
#define BLOCK_ROTATE_STAIRS_TOP 4
#define STAIRS_MAX_BOXES 3

typedef enum {
    STAIRS_STRAIGHT = 0,
    STAIRS_INNER_LEFT,
    STAIRS_INNER_RIGHT,
    STAIRS_OUTER_LEFT,
    STAIRS_OUTER_RIGHT,
} StairShape;

// MC's getStairsShape: stair_rots[d] is the full rotation of the stair cell
// in world direction d (d indexes the same 0=-z/1=+x/2=+z/3=-x space as
// rotation & 3), or -1 when the neighbor isn't a stair. A corner needs a
// perpendicular-facing stair of the same half on the front (outer) or back
// (inner) side, and is suppressed when the cell on the merge's third side is
// a same-facing stair of the same half.
StairShape stairs_shape_derive( unsigned char rotation, const int stair_rots[ 4 ] );
// The stair's definition-space boxes (rotation 0 = facing -z, the full-height
// half at the low-z edge): box 0 is the half slab, boxes 1-2 the step(s);
// top_half flips the model upside-down (BLOCK_ROTATE_STAIRS_TOP). skip_faces
// kills the faces coplanar with the slab surface or the inner seam. Returns
// the box count (2 straight/outer, 3 inner).
int stairs_boxes( StairShape shape, bool top_half, BlockBox out[ STAIRS_MAX_BOXES ] );
// Def->world yaw for a pixel-space box — the same scale/offset remap the
// mesh shader's face_shift rotation and collision/raycast apply:
// ROTATE_90/270 swap the x/z axes, ROTATE_180 mirrors both.
static inline void block_rotate_box( unsigned char yaw, signed char scale[ 3 ], signed char offset[ 3 ] ) {
    if ( yaw == BLOCK_ROTATE_90 ) {
        const signed char sx = scale[ 0 ], ox = offset[ 0 ];
        scale[ 0 ] = scale[ 2 ];
        scale[ 2 ] = sx;
        offset[ 0 ] = static_cast<signed char>( 16 - scale[ 0 ] - offset[ 2 ] );
        offset[ 2 ] = ox;
    } else if ( yaw == BLOCK_ROTATE_180 ) {
        offset[ 0 ] = static_cast<signed char>( 16 - scale[ 0 ] - offset[ 0 ] );
        offset[ 2 ] = static_cast<signed char>( 16 - scale[ 2 ] - offset[ 2 ] );
    } else if ( yaw == BLOCK_ROTATE_270 ) {
        const signed char sx = scale[ 0 ], ox = offset[ 0 ];
        scale[ 0 ] = scale[ 2 ];
        scale[ 2 ] = sx;
        offset[ 0 ] = offset[ 2 ];
        offset[ 2 ] = static_cast<signed char>( 16 - sx - ox );
    }
}
// World-space stair boxes: derive the corner shape, emit the def-space
// boxes, yaw each by rotation & 3. Collision and raycast consume these; the
// mesher emits the def-space stairs_boxes and lets the shader yaw them.
int stairs_collision_boxes( unsigned char rotation, const int stair_rots[ 4 ], BlockBox out[ STAIRS_MAX_BOXES ] );
// Icon/pickup shape for a stair item: the lone straight bottom-half stair,
// yawed 270 so the staircase profile faces +z — the isometric icon view's
// FRONT — and the steps read ascending to the left. Returns the box count
// (2: slab + step).
int stairs_icon_boxes( BlockBox out[ STAIRS_MAX_BOXES ] );

// ---- Standing signs ---------------------------------------------------------
// The two definition-space boxes of a standing sign: box 0 is the 16x9x2
// board plate at the top of the cell (offset y 7, matching the wall sign's
// plate), box 1 the 2x7x2 post under its middle. The post's top is coplanar
// with the board's bottom quad — skip_faces drops it — and its bottom sits
// coplanar on the supporting block's top face, so that cap is skipped too.
// Every lateral quad samples its side tile at the model-space rows/cols it
// covers (tex_offset = -offset), so the tile lays the board band over the
// post column; the shader yaws the pair with face_shift like fences/stairs.
#define SIGN_MAX_BOXES 3
int standing_sign_boxes( BlockBox out[ SIGN_MAX_BOXES ] );

// ---- Hanging signs ----------------------------------------------------------
// The three definition-space boxes of a hanging sign: box 0 is the same
// 16x9x2 board plate the standing sign mounts, parked at the bottom of the
// cell (y 0..8); boxes 1-2 are 3x7x3 chain strands rising from the board's
// top edge to the cell's top face (the support sits overhead). The strands'
// caps are skipped — top is coplanar with the support's bottom face, bottom
// with the board's top quad. The shader yaws the trio with face_shift like
// the standing pair.
int hanging_sign_boxes( BlockBox out[ SIGN_MAX_BOXES ] );

// ---- Item icons -------------------------------------------------------------
// The model-space boxes a block's item icon draws: stairs their
// stairs_icon_boxes, fences a connected z segment (fence_icon_boxes),
// gates the closed leaf spanning z (fence_gate_icon_boxes). Returns the
// box count — 0 for blocks whose icon is their single def box
// (Block::scale/offset/tex_offset), which callers draw through the
// non-box path.
int block_icon_boxes( const Block *block, BlockBox out[ ICON_MAX_BOXES ] );

// ---- Slabs ------------------------------------------------------------------
// Half slabs store the vertical half in rotation bit 2 — the same bit the
// stair top half and gate/door flags reuse on their own types. The yaw bits
// are meaningless on a slab, so block_yaw masks the whole rotation away.
#define BLOCK_ROTATE_SLAB_TOP BLOCK_ROTATE_STAIRS_TOP

// ---- Doors -----------------------------------------------------------------
// Doors share the gate's flag bit: the lower half's rotation bit 2 is the
// open flag, the upper's is hinge-right; bits 0-1 of both hold the slab's
// current yaw (the facing it would have if placed with that yaw). The door's
// placement facing F is recovered by the toggle logic only.
#define BLOCK_ROTATE_DOOR_FLAG 4

// World yaw of the door slab: closed it's the placement facing; open it
// swings 90° — +1 for a left hinge, -1 for a right one (MC's door shape
// table: an open door equals the closed slab of a neighbouring facing).
static inline unsigned char door_yaw( unsigned char facing, bool open, bool hinge_right ) {
    return open ? static_cast<unsigned char>( ( facing + ( hinge_right ? 3 : 1 ) ) & 3 ) : facing;
}

// Per-half rotations for a door state: lower = yaw|open, upper = yaw|hinge.
static inline void door_rotations( unsigned char facing, bool open, bool hinge_right, unsigned char &lower_rot, unsigned char &upper_rot ) {
    const unsigned char yaw = door_yaw( facing, open, hinge_right );
    lower_rot = static_cast<unsigned char>( yaw | ( open ? BLOCK_ROTATE_DOOR_FLAG : 0 ) );
    upper_rot = static_cast<unsigned char>( yaw | ( hinge_right ? BLOCK_ROTATE_DOOR_FLAG : 0 ) );
}

// Recover the placement facing from a door pair's rotations.
static inline unsigned char door_facing( unsigned char lower_rot, unsigned char upper_rot ) {
    const unsigned char yaw = lower_rot & 3;
    if ( !( lower_rot & BLOCK_ROTATE_DOOR_FLAG ) ) {
        return yaw; // closed: the yaw IS the facing
    }
    return static_cast<unsigned char>( ( yaw + ( ( upper_rot & BLOCK_ROTATE_DOOR_FLAG ) ? 1 : 3 ) ) & 3 );
}

// A tall plant is a two-cell foliage pair (tall grass): the lower and
// upper halves link through tall_plant_partner, exactly one flagged
// is_tall_plant_upper.
static inline bool block_is_tall_plant( const Block *b ) {
    return b->tall_plant_partner != LAST_BLOCK_ID;
}

// Natural (decaying) leaves point at their player-placed twin.
static inline bool block_is_natural_leaf( const Block *b ) {
    return b->persistent_variant != LAST_BLOCK_ID;
}

// Player-placed (persistent) leaves point back at the natural variant.
static inline bool block_is_persistent_leaf( const Block *b ) {
    return b->natural_variant != LAST_BLOCK_ID;
}

// Any leaf, natural or persistent.
static inline bool block_is_leaf( BlockID id ) {
    const Block *b = block_definition_get_definition( id );
    return block_is_natural_leaf( b ) || block_is_persistent_leaf( b );
}

// The natural id a placed/persistent leaf decays-as: natural leaves map to
// themselves, persistent leaves to their twin.
static inline BlockID block_natural_leaf_id( BlockID id ) {
    const Block *b = block_definition_get_definition( id );
    return block_is_persistent_leaf( b ) ? b->natural_variant : id;
}

// The block actually written to the world when a leaf item is placed:
// placing natural leaves always stores the persistent variant.
static inline BlockID block_placed_leaf_id( BlockID id ) {
    const Block *b = block_definition_get_definition( id );
    return block_is_natural_leaf( b ) ? b->persistent_variant : id;
}

// The inventory block for a multi-cell block: breaking or picking a door's
// or tall plant's upper half gives the lower's item; persistent leaves
// give the natural leaf item; a wall-mounted sign variant gives the shared
// standing-sign item.
static inline BlockID block_item_id( BlockID id ) {
    const Block *b = block_definition_get_definition( id );
    if ( b->is_door_upper ) {
        return b->door_partner;
    }
    if ( b->is_tall_plant_upper ) {
        return b->tall_plant_partner;
    }
    if ( b->is_bed_head ) {
        return b->bed_partner;
    }
    if ( block_is_persistent_leaf( b ) ) {
        return b->natural_variant;
    }
    if ( b->standing_variant != LAST_BLOCK_ID ) {
        return b->standing_variant;
    }
    return id;
}

// The cell offset from a bed half to its partner: a foot's head cell sits
// one block in the placer's facing direction (rotation bits 0-1, yaw 0 =
// -z); a head's foot sits the same vector negated. Only meaningful for
// blocks with bed_partner set.
static inline glm::ivec3 block_bed_partner_dir( const Block *b, unsigned char rotation ) {
    static const int yaw_to_face[ 4 ] = { FACE_BACK, FACE_RIGHT, FACE_FRONT, FACE_LEFT };
    const int face = yaw_to_face[ rotation & 3 ];
    const glm::ivec3 dir( FACE_DIR_X_OFFSETS[ face ], FACE_DIR_Y_OFFSETS[ face ], FACE_DIR_Z_OFFSETS[ face ] );
    return b->is_bed_head ? -dir : dir;
}

// A sign-style item covers a floor block and a wall block: a side-face
// click (mount_dir horizontal) mounts the wall variant on the clicked
// block. Returns the yaw that attaches it, or -1 when the block has no
// wall variant or the clicked face isn't a wall mount — the caller then
// keeps the standing block.
static inline int block_wall_variant_rotation( const Block *block, const glm::ivec3 &mount_dir ) {
    if ( block->wall_variant == LAST_BLOCK_ID ) {
        return -1;
    }
    return block_mount_rotation( block_definition_get_definition( block->wall_variant ), mount_dir );
}

// The yaw part of a rotation for single-box consumers (collision, raycast,
// selection, debris): an open gate or toggled door keeps a yaw and must
// never reach the rotation >= 4 wall-torch branches. Slabs mask to 0 — their
// rotation is only the top-half bit (BLOCK_ROTATE_SLAB_TOP), which those
// consumers read off the raw rotation instead. Cake masks to 0 too — its
// rotation is the bite count, not an orientation.
static inline unsigned char block_yaw( const Block *b, unsigned char rot ) {
    return ( b->is_fence_gate || b->is_door || b->is_stairs || b->is_slab ) ? ( rot & 3 ) : ( b->is_cake ? 0 : rot );
}

// face_shift flag bits: the low nibble holds the yaw rotation (0-7).
#define BLOCK_COORDS_ROTATION_MASK 0xfu
// Mirror the unit cube so every quad faces inward (inward-facing shells).
#define BLOCK_COORDS_INSIDE_OUT 0x10u
// scale/offset/tex_offset are in 1/32 block instead of 1/16.
#define BLOCK_COORDS_HALF_PIXEL 0x20u
// Fluid cell: the vd_data_water cube's y=1 verts drop to the per-corner
// surface heights in fluid_h0/fluid_h (1/64 block units) and the flow
// texture animates along the surface gradient.
#define BLOCK_COORDS_FLUID 0x40u

// See world.cpp for vbl
typedef struct {
    float x;
    float y;
    float z;

    unsigned char mesh_x;
    unsigned char mesh_y;
    unsigned char mesh_z;
    // Fluid corner heights in 1/64 block units (BLOCK_COORDS_FLUID
    // instances only — free space otherwise). Corner order for fluid_h0 +
    // fluid_h[0..2] is (x=0,z=0),(x=1,z=0),(x=0,z=1),(x=1,z=1).
    unsigned char fluid_h0;

    unsigned short face[ NUM_FACES_IN_CUBE ];
    unsigned int packed_lighting[ NUM_FACES_IN_CUBE ];
    unsigned int face_shift;

    // Pixel values (0-16 range, offset can be slightly negative for outsets).
    // Uploaded as signed bytes; the shader divides by 16.0 to get floats.
    signed char scale_x;
    signed char scale_y;
    signed char scale_z;

    signed char offset_x;
    signed char offset_y;
    signed char offset_z;

    signed char tex_offset_x;
    signed char tex_offset_y;
    signed char tex_offset_z;

    // Corners (x=1,z=0),(x=0,z=1),(x=1,z=1) — see fluid_h0 above. Bound as
    // uvec3 at location 13 (world.cpp vbl_coords push_byte). Aligns the
    // struct to 4 bytes (stride = 68).
    unsigned char fluid_h[ 3 ];

} BlockCoords;

// The instance vbl in world.cpp binds locations 4-13 against this exact
// layout — grow fields only into the fluid_h0/fluid_h slots.
static_assert( sizeof( BlockCoords ) == 68, "BlockCoords stride must stay 68 (vbl_coords in world.cpp)" );

void block_adjust_coord_based_on_state( const Block *block, const BlockState *blockState, BlockCoords *blockCoord );