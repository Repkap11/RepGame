#version 300 es

uniform mat4 u_MVP;
uniform vec3 u_Origin;

// See CubeFace in block.h
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoordBlock;
layout( location = 2 ) in uint faceType;
layout( location = 3 ) in uint corner_shift;

// See BlockCoords in chunk.h
layout( location = 4 ) in vec3 blockCoords;
layout( location = 5 ) in uint mesh_size_packed;
layout( location = 6 ) in uvec3 blockTexture;
layout( location = 7 ) in uvec3 packed_lighting_1;
layout( location = 8 ) in uvec3 packed_lighting_2;
layout( location = 9 ) in uint rotation;
layout( location = 10 ) in ivec3 blockCoords_scale;
layout( location = 11 ) in ivec3 blockCoords_offset;
layout( location = 12 ) in ivec3 texCoords_offset;

// Raw (pre-transform) position passed to the fragment shader so it can
// compute screen-space distance to the cube edges (at -s and 1+s).
out vec3 v_local_pos;
flat out uint v_faceType;

#define BLOCK_ROTATE_0 0u
#define BLOCK_ROTATE_90 1u
#define BLOCK_ROTATE_180 2u
#define BLOCK_ROTATE_270 3u

void main( ) {
    vec3 mesh_size = vec3( ( mesh_size_packed & 0xffu ), ( mesh_size_packed & 0xff00u ) >> 8, ( mesh_size_packed & 0xff0000u ) >> 16 );
    vec3 meshed_position = position * mesh_size;

    vec3 adjusted_position = meshed_position;

    vec3 blockCoords_scale_f = vec3( blockCoords_scale ) / 16.0f;
    vec3 blockCoords_offset_f = vec3( blockCoords_offset ) / 16.0f;

    vec3 blockCoords_scale_adjust = blockCoords_scale_f;
    vec3 blockCoords_offset_adjust = blockCoords_offset_f;

    if ( rotation == BLOCK_ROTATE_0 ) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.xz;
    } else if ( rotation == BLOCK_ROTATE_90 ) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.zx;
        blockCoords_offset_adjust.x = 1.0f - blockCoords_scale_f.z - blockCoords_offset_f.z;
        blockCoords_offset_adjust.z = blockCoords_offset_f.x;
    } else if ( rotation == BLOCK_ROTATE_180 ) {
        blockCoords_scale_adjust.xz = blockCoords_scale_f.xz;
        blockCoords_offset_adjust.x = 1.0f - blockCoords_scale_f.x - blockCoords_offset_f.x;
        blockCoords_offset_adjust.z = 1.0f - blockCoords_scale_f.z - blockCoords_offset_f.z;
    } else if ( rotation == BLOCK_ROTATE_270 ) {
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

    vec3 world_coords = adjusted_position + blockCoords - u_Origin;
    gl_Position = u_MVP * vec4( world_coords, 1.0f );

    v_local_pos = position;
    v_faceType = faceType;
}
