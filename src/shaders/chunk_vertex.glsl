#version 300 es
uniform mat4 u_MVP;
uniform vec3 u_DebugScaleOffset;
uniform float u_ReflectionHeight;
// Floating-origin offset (integer-valued, snapped to chunk grid). Subtracted
// from absolute block coordinates so all GPU math stays near zero regardless
// of how far the player is from the world origin. Exact in float32 because
// both blockCoords and u_Origin are integer-valued and < 2^24.
uniform vec3 u_Origin;

#define MAX_ROTATABLE_BLOCK 100u
uniform float u_RandomRotationBlocks[MAX_ROTATABLE_BLOCK];

// See CubeFace in block.h
// This defines all the triangles of a cube
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texCoordBlock;
layout(location = 2) in uint faceType;
layout(location = 3) in uint corner_shift;

// See BlockCoords in chunk.h
// There is one of these per block
layout(location = 4) in vec3 blockCoords;
layout(location = 5) in uint mesh_size_packed;
layout(location = 6) in uvec3 blockTexture;
layout(location = 7) in uvec3 packed_lighting_1;
layout(location = 8) in uvec3 packed_lighting_2;
layout(location = 9) in uint rotation;
layout(location = 10) in ivec3 blockCoords_scale;
layout(location = 11) in ivec3 blockCoords_offset;
layout(location = 12) in ivec3 texCoords_offset;

out vec2 v_TexCoordBlock;
flat out uint v_blockID;
out float v_planarDot;
out float v_corner_lighting;
#if !defined(REPGAME_LOW_GRAPHICS)
flat out float v_center_lighting;
#endif
out vec3 v_world_coords;
// Outward face normal (post block rotation). The fragment shader samples the
// flood-fill light volume at the air cell this face points into.
flat out vec3 v_face_normal;

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

#define NO_LIGHT_NO_DRAW 0x6fffffu

#define CORNER_OFFSET_c 16u

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

    vec3 mesh_size = vec3((mesh_size_packed & 0xffu), (mesh_size_packed & 0xff00u) >> 8, (mesh_size_packed & 0xff0000u) >> 16);
    vec3 meshed_position = position * (mesh_size - u_DebugScaleOffset);

    vec3 adjusted_position = meshed_position;

    vec3 blockCoords_scale_f = vec3(blockCoords_scale) / 16.0f;
    vec3 blockCoords_offset_f = vec3(blockCoords_offset) / 16.0f;
    vec3 texCoords_offset_f = vec3(texCoords_offset) / 16.0f;

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
    v_planarDot = dot(vertex, vec4(0, 1, 0, u_ReflectionHeight));

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
    float face_light;
    if(faceType == FACE_TOP) {
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
    v_face_normal = face_normal;

    float light_divisor = 1.3f; // good looking
    // light_divisor = 0.5;       // debug

    uint which_bits = 3u;
    if(corner_shift == CORNER_OFFSET_c) {
        which_bits = 7u;
    }
    float corner_light = float((packed_lighting >> corner_shift) & which_bits) / light_divisor; // Has to be a float to be interped over the shader;
    if(corner_shift == CORNER_OFFSET_c) {
        corner_light /= 2.0f;
    }
    // AO strength halved: the flood-fill light field now provides the real
    // spatial shadowing, so ambient occlusion only needs to add subtle
    // crevice darkening instead of acting as the primary shadow source
    // (stacking both at full strength double-darkened occluded corners).
    corner_light = (3.9f - corner_light * 0.5f) / 3.9f;

    v_corner_lighting = face_light * corner_light;

    // Average of all 4 corner light values for this face (flat, same for
    // all vertices). Precomputed on the CPU and packed at CORNER_OFFSET_avg.
    // Used at grazing angles to avoid shimmer from perspective-correct
    // interpolation of per-corner lighting.
#if !defined(REPGAME_LOW_GRAPHICS)
    float avg_light = float((packed_lighting >> 21u) & 3u) / light_divisor;
    avg_light = (3.9f - avg_light * 0.5f) / 3.9f;
    v_center_lighting = face_light * avg_light;
#endif
    v_TexCoordBlock = texCoordBlock_adjust * face_scale - face_shift;

    uint shift = (faceType_rotated % 2u) * 16u;
    // 0xffffu is max short 16u is sizeof(short)
    v_blockID = (blockTexture[faceType_rotated / 2u] & (0xffffu << shift)) >> shift;
    // if ( faceType < 2u ) {
    //     v_blockID = blockTexture1[ faceType ];
    // } else {
    //     v_blockID = blockTexture2[ faceType - 3u ];
    // }
    if(v_blockID >= MAX_ROTATABLE_BLOCK) {
        // v_blockID is the texture layer (textures[face] - 1): ids >=
        // MAX_ROTATABLE_BLOCK index past the end of this 100-element array
        // (e.g. SNOWY_GRASS sides = 100), reading whatever adjacent uniform
        // the driver hands back and randomly rotating those faces.
        v_block_auto_rotates = 0;
    } else {
        v_block_auto_rotates = int(u_RandomRotationBlocks[v_blockID] == 1.0f);
    }
}
