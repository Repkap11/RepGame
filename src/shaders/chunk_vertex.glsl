#version 300 es
uniform mat4 u_MVP;
uniform vec3 u_DebugScaleOffset;
uniform float u_ReflectionHeight;
// REPGAME_TRANSLUCENT_AFTER_WATER=split: sea-level water plane (rebased) for
// the per-fragment before/after-water split (u_SplitSign in the fragment).
uniform float u_SplitY;
// Floating-origin offset (integer-valued, snapped to chunk grid). Subtracted
// from absolute block coordinates so all GPU math stays near zero regardless
// of how far the player is from the world origin. Exact in float32 because
// both blockCoords and u_Origin are integer-valued and < 2^24.
uniform vec3 u_Origin;
// Opaque pass only: push each quad corner outward in *screen space* by
// this many pixels so greedy-meshed neighbours overlap on screen and
// T-junction pinholes close — including faces seen nearly edge-on, where a
// world-space grow barely changes coverage. 0 for water/translucent/flowers
// (blended passes would double-blend the grown overlaps).
uniform float u_FaceGrowPx;
// Current render-target size in pixels (for the screen-space grow).
uniform vec2 u_ViewportSize;

#include "common/texture_ids_gen.hpp"

// See CubeFace in block.h
// This defines all the triangles of a cube
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoordBlock;
layout(location = 2) in uint faceType;

// See BlockCoords in chunk.h
// There is one of these per block
layout(location = 4) in vec3 blockCoords;
layout(location = 5) in uint mesh_size_packed;
layout(location = 6) in uvec3 blockTexture;
layout(location = 7) in uvec3 packed_lighting_1;
layout(location = 8) in uvec3 packed_lighting_2;
layout(location = 9) in uint rotation_packed;
layout(location = 10) in ivec3 blockCoords_scale;
layout(location = 11) in ivec3 blockCoords_offset;
layout(location = 12) in ivec3 texCoords_offset;
// Corners (x=1,z=0),(x=0,z=1),(x=1,z=1) of the fluid surface in 1/64 block
// units — only read for BLOCK_COORDS_FLUID instances (corner (x=0,z=0)
// rides in the top byte of mesh_size_packed).
layout(location = 13) in uvec3 fluid_h;

out vec2 v_TexCoordBlock;
flat out vec2 v_tex_min;
flat out vec2 v_tex_max;
flat out uint v_blockID;
out float v_planarDot;
out float v_SplitDot;
out float v_face_light;
out vec3 v_world_coords;
// Outward face normal (post block rotation). The fragment shader samples the
// flood-fill light volume at the air cell this face points into.
flat out vec3 v_face_normal;
// World position the fragment shader samples the light volume at.
out vec3 v_light_pos;
// World-xz flow direction of a fluid surface (0,0 when level). The top
// face's UVs are rotated so +v points along it — fragment scrolling along
// v is scrolling along the flow.
flat out vec2 v_flow_dir;

flat out int v_block_auto_rotates;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

#define BLOCK_ROTATE_0 0u
#define BLOCK_ROTATE_90 1u
#define BLOCK_ROTATE_180 2u
#define BLOCK_ROTATE_270 3u

// Must match block.hpp BlockCoords face_shift flag bits.
#define BLOCK_COORDS_ROTATION_MASK 0xfu
#define BLOCK_COORDS_INSIDE_OUT 0x10u
#define BLOCK_COORDS_HALF_PIXEL 0x20u
#define BLOCK_COORDS_FLUID 0x40u

#define NO_LIGHT_NO_DRAW 0x6fffffu

// Must match the flower verts at the end of vd_data_solid in block.hpp.
#define FLOWER_FIRST_VERTEX 24

void main() {
    uint packed_lighting;
    if(faceType == FACE_TOP || faceType == FACE_BOTTOM || faceType == FACE_RIGHT) {
        packed_lighting = packed_lighting_1[faceType];
    } else { // For FACE_FRONT FACE_LEFT FACE_BACK
        packed_lighting = packed_lighting_2[faceType - 3u];
    }
    if(packed_lighting == NO_LIGHT_NO_DRAW) {
        // Emit a vertex strictly outside the clip volume (z > w) instead of
        // vec4(0): w=0 produces NaN clip coordinates whose rasterization is
        // undefined on some drivers.
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }

    uint rotation = rotation_packed & BLOCK_COORDS_ROTATION_MASK;
    bool inside_out = (rotation_packed & BLOCK_COORDS_INSIDE_OUT) != 0u;
    float unit_div = (rotation_packed & BLOCK_COORDS_HALF_PIXEL) != 0u ? 32.0 : 16.0;
    // Mirroring all three axes keeps the box in the same space but flips the
    // triangle winding, so the back-face cull drops the near walls of an
    // inside-out shell and each quad's faceType normal points inward.
    vec3 cube_pos = inside_out ? vec3(1.0) - position : position;

    vec3 mesh_size = vec3((mesh_size_packed & 0xffu), (mesh_size_packed & 0xff00u) >> 8, (mesh_size_packed & 0xff0000u) >> 16);
    vec3 meshed_position = cube_pos * (mesh_size - u_DebugScaleOffset);

    // Fluid surfaces: per-corner heights (1/64 units) replace the unit
    // cube's top edge. h0 lives in the free top byte of mesh_size_packed,
    // h1..h3 in fluid_h — corner order (x=0,z=0),(x=1,z=0),(x=0,z=1),
    // (x=1,z=1), matching the BlockCoords packing.
    bool fluid_instance = (rotation_packed & BLOCK_COORDS_FLUID) != 0u;
    float fluid_height = 0.0;
    v_flow_dir = vec2(0.0);
    if(fluid_instance) {
        float h[4];
        h[0] = float(mesh_size_packed >> 24) / 64.0;
        h[1] = float(fluid_h.x) / 64.0;
        h[2] = float(fluid_h.y) / 64.0;
        h[3] = float(fluid_h.z) / 64.0;
        int hidx = int(cube_pos.x > 0.5) | (int(cube_pos.z > 0.5) << 1);
        fluid_height = h[hidx];
        // Flow direction: downhill, from the high corners toward the low
        // ones — the negated surface gradient in world xz.
        vec2 grad = vec2((h[0] + h[2]) - (h[1] + h[3]), (h[0] + h[1]) - (h[2] + h[3]));
        if(dot(grad, grad) > 1e-4) {
            v_flow_dir = normalize(grad);
        }
        if(cube_pos.y > 0.5) {
            // Top-edge verts ride the corner height; a merged column's top
            // lands on the top cell's surface (h plus the cells below).
            meshed_position.y = cube_pos.y * (mesh_size.y - 1.0) + fluid_height;
        }
    }

    vec3 adjusted_position = meshed_position;

    vec3 blockCoords_scale_f = vec3(blockCoords_scale) / unit_div;
    vec3 blockCoords_offset_f = vec3(blockCoords_offset) / unit_div;
    vec3 texCoords_offset_f = vec3(texCoords_offset) / unit_div;

    vec3 blockCoords_scale_adjust = blockCoords_scale_f;
    vec3 blockCoords_offset_adjust = blockCoords_offset_f;

    if(rotation == BLOCK_ROTATE_0) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.xz;
    } else if(rotation == BLOCK_ROTATE_90) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.zx;
        blockCoords_offset_adjust.x = 1.0f - blockCoords_scale_f.z - blockCoords_offset_f.z;
        blockCoords_offset_adjust.z = blockCoords_offset_f.x;
    } else if(rotation == BLOCK_ROTATE_180) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.xz;
        blockCoords_offset_adjust.x = 1.0f - blockCoords_scale_f.x - blockCoords_offset_f.x;
        blockCoords_offset_adjust.z = 1.0f - blockCoords_scale_f.z - blockCoords_offset_f.z;
    } else if(rotation == BLOCK_ROTATE_270) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.zx;
        blockCoords_offset_adjust.x = blockCoords_offset_f.z;
        blockCoords_offset_adjust.z = 1.0f - blockCoords_scale_f.x - blockCoords_offset_f.x;
    }

    adjusted_position.x *= blockCoords_scale_adjust.x;
    adjusted_position.y *= blockCoords_scale_adjust.y;
    adjusted_position.z *= blockCoords_scale_adjust.z;

    adjusted_position.x += blockCoords_offset_adjust.x;
    adjusted_position.y += blockCoords_offset_adjust.y;
    adjusted_position.z += blockCoords_offset_adjust.z;

    v_world_coords = adjusted_position + blockCoords - u_Origin;

    vec4 vertex = vec4(v_world_coords, 1);
    gl_Position = u_MVP * vertex;
    if(u_FaceGrowPx > 0.0) {
        // Screen-space grow: push this corner outward along both quad edges by
        // u_FaceGrowPx pixels. The neighbouring corners differ only in this
        // corner's 0/1 coordinate along each tangent axis. Depth is untouched.
        vec3 face_extent = (mesh_size - u_DebugScaleOffset) * blockCoords_scale_adjust;
        vec3 face_axis_a = (faceType == FACE_RIGHT || faceType == FACE_LEFT) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 face_axis_b = (faceType == FACE_FRONT || faceType == FACE_BACK) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
        vec4 clip_a = u_MVP * vec4(v_world_coords + face_axis_a * face_extent * (1.0 - 2.0 * cube_pos), 1.0);
        vec4 clip_b = u_MVP * vec4(v_world_coords + face_axis_b * face_extent * (1.0 - 2.0 * cube_pos), 1.0);
        if(gl_Position.w > 1e-4 && clip_a.w > 1e-4 && clip_b.w > 1e-4) {
            vec2 half_vp = 0.5 * u_ViewportSize;
            vec2 px = gl_Position.xy / gl_Position.w * half_vp;
            vec2 edge_a = px - clip_a.xy / clip_a.w * half_vp;
            vec2 edge_b = px - clip_b.xy / clip_b.w * half_vp;
            // Move each of this corner's two edges outward along its own
            // screen-space normal by u_FaceGrowPx (miter offset), so a face
            // seen nearly edge-on gets thicker instead of folding. Each normal
            // points away from the quad interior (the side the other edge lies on).
            vec2 n_a = vec2(-edge_a.y, edge_a.x);
            vec2 n_b = vec2(-edge_b.y, edge_b.x);
            float len_na = length(n_a);
            float len_nb = length(n_b);
            if(len_na > 1e-3 && len_nb > 1e-3) {
                n_a = n_a / len_na * sign(dot(n_a, edge_b));
                n_b = n_b / len_nb * sign(dot(n_b, edge_a));
                vec2 push = (n_a + n_b) / max(1.0 + dot(n_a, n_b), 0.25);
                gl_Position.xy += push * (u_FaceGrowPx / half_vp) * gl_Position.w;
            }
        }
    }
    v_planarDot = dot(vertex, vec4(0, 1, 0, u_ReflectionHeight));
    v_SplitDot = vertex.y - u_SplitY;

    vec2 face_scale = vec2(1, 1);
    vec2 face_shift;
    vec2 texCoordBlock_adjust = texCoordBlock;
    uint faceType_rotated = faceType;
    if(faceType == FACE_TOP || faceType == FACE_BOTTOM) {
        face_shift = texCoords_offset_f.xz;
        if(rotation == BLOCK_ROTATE_0) {
            face_scale.xy = mesh_size.xz;
            texCoordBlock_adjust = vec2(texCoordBlock.x, texCoordBlock.y);
        } else if((faceType == FACE_TOP && rotation == BLOCK_ROTATE_270) || (faceType == FACE_BOTTOM && rotation == BLOCK_ROTATE_90)) {
            face_scale.xy = mesh_size.zx;
            blockCoords_scale_adjust.xz = blockCoords_scale_adjust.zx;
            texCoordBlock_adjust = vec2(1.0f - texCoordBlock.y, texCoordBlock.x);
        } else if(rotation == BLOCK_ROTATE_180) {
            face_scale.xy = mesh_size.xz;
            texCoordBlock_adjust = vec2(1.0f - texCoordBlock.x, 1.0f - texCoordBlock.y);
        } else if((faceType == FACE_TOP && rotation == BLOCK_ROTATE_90) || (faceType == FACE_BOTTOM && rotation == BLOCK_ROTATE_270)) {
            face_scale.xy = mesh_size.zx;
            blockCoords_scale_adjust.xz = blockCoords_scale_adjust.zx;
            texCoordBlock_adjust = vec2(texCoordBlock.y, 1.0f - texCoordBlock.x);
        } else {
            // Non-yaw rotations (piston up/down = 4/5): the box is already
            // world-baked by block_adjust — no texcoord remap, just keep the
            // merged-quad extent so the tile repeats per cell instead of
            // stretching over the whole run.
            face_scale.xy = mesh_size.xz;
        }
        face_scale.x *= blockCoords_scale_adjust.x;
        face_scale.y *= blockCoords_scale_adjust.z;
    } else if(faceType == FACE_RIGHT || faceType == FACE_LEFT) {
        faceType_rotated = (faceType - rotation - 2u) % 4u + 2u;
        face_scale.xy = mesh_size.zy;
        face_scale.x *= blockCoords_scale_adjust.z;
        face_scale.y *= blockCoords_scale_adjust.y;
        if(rotation == BLOCK_ROTATE_90 || rotation == BLOCK_ROTATE_270) {
            face_shift = texCoords_offset_f.zy;
        } else {
            face_shift = texCoords_offset_f.xy;
        }

    } else if(faceType == FACE_FRONT || faceType == FACE_BACK) {
        faceType_rotated = (faceType - rotation - 2u) % 4u + 2u;
        face_scale.xy = mesh_size.xy;
        face_scale.x *= blockCoords_scale_adjust.x;
        face_scale.y *= blockCoords_scale_adjust.y;

        if(rotation == BLOCK_ROTATE_90 || rotation == BLOCK_ROTATE_270) {
            face_shift = texCoords_offset_f.xy;
        } else {
            face_shift = texCoords_offset_f.zy;
        }
    }
    if(fluid_instance) {
        if(faceType == FACE_TOP && dot(v_flow_dir, v_flow_dir) > 0.5) {
            // Rotate the top quad's UVs so +v points along the flow — the
            // strip scroll (fragment shader, always along v) then reads
            // downstream on any surface slope. v_flow_dir is the normalized
            // (cos,sin) in the u=x / v=z basis. Flat surfaces (0,0) skip
            // this: (0,0) collapses every UV to one texel, and (1,0) is a
            // u/v swap, not identity — on a greedy-merged flat quad the
            // swap squeezes the tile along the long axis into thin
            // streaks. Sloped cells are always 1x1, where the swap is benign.
            float c = v_flow_dir.x, s = v_flow_dir.y;
            texCoordBlock_adjust = vec2(
                -texCoordBlock_adjust.x * s + texCoordBlock_adjust.y * c,
                texCoordBlock_adjust.x * c + texCoordBlock_adjust.y * s);
        } else if(cube_pos.y > 0.5 && faceType != FACE_TOP && faceType != FACE_BOTTOM) {
            // Side-top verts: the flow strip runs v=0..1 over one cell, so
            // scale v by the corner height — a 56/64 surface samples the
            // upper 0.875 of the strip just like the old fixed-height
            // water did with tex_coords_size.y. FACE_TOP must stay out of
            // this branch: flat surfaces (v_flow_dir = 0) skip the rotation
            // above, and scaling their v by the corner height collapses the
            // top tile to a thin strip — stretched lines on shallow flows.
            texCoordBlock_adjust.y *= fluid_height;
        }
    }
    // Minecraft shades water shade=false: every face at full brightness.
    // Directional shading on the translucent shell makes the internal
    // surfaces brighter than the walls they show through — per-cell
    // light/dark banding that reads as lines on a flow. Flat merged cells
    // aren't BLOCK_COORDS_FLUID, so test the face's texture layer.
    uint shift_early = (faceType_rotated % 2u) * 16u;
    uint face_tex = (blockTexture[faceType_rotated / 2u] & (0xffffu << shift_early)) >> shift_early;
    bool is_fluid_face = face_tex == uint(TEX_WATER_STILL) || face_tex == uint(TEX_WATER_FLOW) ||
                         face_tex == uint(TEX_LAVA_STILL) || face_tex == uint(TEX_LAVA_FLOW);
    float face_light;
    if(is_fluid_face) {
        face_light = 1.0f;
    } else if(faceType == FACE_TOP) {
        face_light = 1.0f;
    } else if(faceType == FACE_BOTTOM) {
        face_light = 0.5f;
    } else {
        face_light = 0.75f;
    }

    // FACE_RIGHT/FRONT/LEFT/BACK = +x/+z/-x/-z. vd_data_solid puts FRONT
    // vertices at z=1.0 / BACK at z=0.0, and chunk.cpp's shading checks the
    // z+1 neighbor for FRONT (f) and z-1 for BACK (ba).
    //
    // Use faceType, NOT faceType_rotated: the rotation transform only
    // rescales/repositions the box via scale/offset, so a quad's plane never
    // leaves its axis — a LEFT quad is always the -x-most plane whatever the
    // yaw. faceType_rotated picks which texture slot shows on that plane;
    // using it here made rotated blocks sample light 90 degrees off-axis
    // (and wall torches, which encode attachment as rotation 4-7, sampled
    // straight into their own wall).
    vec3 face_normal;
    if(faceType == FACE_TOP) {
        face_normal = vec3(0.0, 1.0, 0.0);
    } else if(faceType == FACE_BOTTOM) {
        face_normal = vec3(0.0, -1.0, 0.0);
    } else if(faceType == FACE_RIGHT) {
        face_normal = vec3(1.0, 0.0, 0.0);
    } else if(faceType == FACE_LEFT) {
        face_normal = vec3(-1.0, 0.0, 0.0);
    } else if(faceType == FACE_FRONT) {
        face_normal = vec3(0.0, 0.0, 1.0);
    } else { // FACE_BACK
        face_normal = vec3(0.0, 0.0, -1.0);
    }
    // The flower cross-quads reuse cube face labels that don't match their
    // diagonal planes, and the flat provoking normal can then differ between
    // a quad's two triangles — a visible diagonal lighting seam. Zero the
    // normal so light is sampled at the fragment's real position and the
    // fragment per-cell hashes land in the flower's own cell. Cross-quads
    // aren't side faces, so they also skip the per-face shading (like
    // Minecraft's cross model, shade=false).
    if(gl_VertexID >= FLOWER_FIRST_VERTEX) {
        face_normal = vec3(0.0);
        face_light = 1.0f;
    }
    v_light_pos = v_world_coords + face_normal * 0.5;
    v_face_normal = face_normal;

    v_face_light = face_light;
    // Bounds of this face's texture region, for the fragment shader to clamp
    // bilinear taps inside it. Non-full shapes (slab bands, chest borders)
    // sit next to transparent texels inside their atlas layer; sampling
    // exactly on the region boundary bleeds that alpha in and drops
    // fragments below the discard threshold — visible as sky-colored
    // hairlines along face edges.
    v_tex_min = -face_shift;
    v_tex_max = face_scale - face_shift;
    v_TexCoordBlock = texCoordBlock_adjust * face_scale - face_shift;

    v_blockID = face_tex;
    // if ( faceType < 2u ) {
    //     v_blockID = blockTexture1[ faceType ];
    // } else {
    //     v_blockID = blockTexture2[ faceType - 3u ];
    // }
    // v_blockID is the 0-based texture layer; the rotatable textures are
    // packed first so a layer rotates iff it's below TEXTURE_ROTATABLE_COUNT.
    v_block_auto_rotates = int(v_blockID < uint(TEXTURE_ROTATABLE_COUNT));
}
