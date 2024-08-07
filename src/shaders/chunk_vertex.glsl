#version 300 es
uniform mat4 u_MVP;
uniform vec3 u_DebugScaleOffset;
uniform float u_ReflectionHeight;

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
layout(location = 10) in vec3 blockCoords_scale;
layout(location = 11) in vec3 blockCoords_offset;
layout(location = 12) in vec3 texCoords_offset;

out vec2 v_TexCoordBlock;
flat out uint v_blockID;
out float v_planarDot;
out float v_corner_lighting;
out vec3 v_world_coords;

flat out int v_needs_rotate;
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

#define NO_LIGHT_NO_DRAW 0xfffffu

#define CORNER_OFFSET_c 16u

// Different devices give different results when using the modulus operator with negative numbers. So I need this function.
int rep_mod(int x, int y) {
    return x - y * int(floor(float(x) / float(y)));
}

void main() {
    uint packed_lighting;
    if(faceType == FACE_TOP || faceType == FACE_BOTTOM || faceType == FACE_RIGHT) {
        packed_lighting = packed_lighting_1[faceType];
    } else { // For FACE_FRONT FACE_LEFT FACE_BACK
        packed_lighting = packed_lighting_2[faceType - 3u];
    }
    if(packed_lighting == NO_LIGHT_NO_DRAW) {
        gl_Position = vec4(0);
        return;
    }

    vec3 mesh_size = vec3((mesh_size_packed & 0xffu), (mesh_size_packed & 0xff00u) >> 8, (mesh_size_packed & 0xff0000u) >> 16);
    vec3 meshed_position = position * (mesh_size - u_DebugScaleOffset);

    vec3 adjusted_position = meshed_position;

    vec3 blockCoords_scale_adjust = blockCoords_scale;
    vec3 blockCoords_offset_adjust = blockCoords_offset;

    if(rotation == BLOCK_ROTATE_0) {
        blockCoords_scale_adjust.xz = blockCoords_scale.xz;
    } else if(rotation == BLOCK_ROTATE_90) {
        blockCoords_scale_adjust.xz = blockCoords_scale.zx;
        blockCoords_offset_adjust.x = 1.0f - blockCoords_scale.z - blockCoords_offset.z;
        blockCoords_offset_adjust.z = blockCoords_offset.x;
    } else if(rotation == BLOCK_ROTATE_180) {
        blockCoords_scale_adjust.xz = blockCoords_scale.xz;
        blockCoords_offset_adjust.x = 1.0f - blockCoords_scale.x - blockCoords_offset.x;
        blockCoords_offset_adjust.z = 1.0f - blockCoords_scale.z - blockCoords_offset.z;
    } else if(rotation == BLOCK_ROTATE_270) {
        blockCoords_scale_adjust.xz = blockCoords_scale.zx;
        blockCoords_offset_adjust.x = blockCoords_offset.z;
        blockCoords_offset_adjust.z = 1.0f - blockCoords_scale.x - blockCoords_offset.x;
    }

    adjusted_position.x *= blockCoords_scale_adjust.x;
    adjusted_position.y *= blockCoords_scale_adjust.y;
    adjusted_position.z *= blockCoords_scale_adjust.z;

    adjusted_position.x += blockCoords_offset_adjust.x;
    adjusted_position.y += blockCoords_offset_adjust.y;
    adjusted_position.z += blockCoords_offset_adjust.z;

    v_world_coords = adjusted_position + blockCoords;

    vec4 vertex = vec4(v_world_coords, 1);
    gl_Position = u_MVP * vertex;
    v_planarDot = dot(vertex, vec4(0, 1, 0, u_ReflectionHeight));

    vec2 face_scale = vec2(1, 1);
    vec2 face_shift;
    vec2 texCoordBlock_adjust = texCoordBlock;
    uint faceType_rotated = faceType;
    if(faceType == FACE_TOP || faceType == FACE_BOTTOM) {
        face_shift = texCoords_offset.xz;
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
            face_shift = texCoords_offset.zy;
        } else {
            face_shift = texCoords_offset.xy;
        }

    } else if(faceType == FACE_FRONT || faceType == FACE_BACK) {
        faceType_rotated = (faceType - rotation - 2u) % 4u + 2u;
        face_scale.xy = mesh_size.xy;
        face_scale.x *= blockCoords_scale_adjust.x;
        face_scale.y *= blockCoords_scale_adjust.y;

        if(rotation == BLOCK_ROTATE_90 || rotation == BLOCK_ROTATE_270) {
            face_shift = texCoords_offset.xy;
        } else {
            face_shift = texCoords_offset.zy;
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
    corner_light = (3.9f - corner_light) / 3.9f;

    v_corner_lighting = face_light * corner_light;
    v_TexCoordBlock = texCoordBlock_adjust * face_scale - face_shift;

    uint shift = (faceType_rotated % 2u) * 16u;
    // 0xffffu is max short 16u is sizeof(short)
    v_blockID = (blockTexture[faceType_rotated / 2u] & (0xffffu << shift)) >> shift;
    // if ( faceType < 2u ) {
    //     v_blockID = blockTexture1[ faceType ];
    // } else {
    //     v_blockID = blockTexture2[ faceType - 3u ];
    // }
    if(v_blockID > MAX_ROTATABLE_BLOCK) {
        v_block_auto_rotates = 0;
    } else {
        v_block_auto_rotates = int(u_RandomRotationBlocks[v_blockID] == 1.0f);
    }
    if(bool(v_block_auto_rotates)) {
        int x_mod = rep_mod(int(blockCoords.x), 32);
        int y_mod = rep_mod(int(blockCoords.y), 32);
        int z_mod = rep_mod(int(blockCoords.z), 32);

        v_needs_rotate = rep_mod(27 * x_mod + 3 * y_mod + z_mod, 32);
    }
}
