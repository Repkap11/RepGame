#version 300 es
uniform mat4 u_MVP;
uniform vec3 u_DebugScaleOffset;

// See CubeFace in block.h
// This defines all the triangles of a cube
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoordBlock;
layout( location = 2 ) in uint faceType;
layout( location = 3 ) in uint corner_shift;

// See BlockCoords in chunk.h
// There is one of these per block
layout( location = 4 ) in vec3 blockCoords;
layout( location = 5 ) in uint mesh_size_packed;
layout( location = 6 ) in uvec3 blockTexture;
layout( location = 7 ) in uvec3 packed_lighting_1;
layout( location = 8 ) in uvec3 packed_lighting_2;
layout( location = 9 ) in mat4 rotation;

out vec2 v_TexCoordBlock;
flat out float v_blockID;
flat out int v_shouldDiscardNoLight;
out float v_corner_lighting;

#define FACE_TOP 0u
#define FACE_BOTTOM 1u
#define FACE_RIGHT 2u
#define FACE_FRONT 3u
#define FACE_LEFT 4u
#define FACE_BACK 5u

#define NO_LIGHT_NO_DRAW 0xfffffu

#define CORNER_OFFSET_c 16u

void main( ) {
    // vec3 adjust = vec3( 1, 1, 1 ) - position;
    // adjust.y = position.y;
    vec3 mesh_size = vec3( ( mesh_size_packed & 0xffu ), ( mesh_size_packed & 0xff00u ) >> 8, ( mesh_size_packed & 0xff0000u ) >> 16 );
    gl_Position = u_MVP * rotation * vec4( position * ( mesh_size - u_DebugScaleOffset ) + blockCoords, 1 );
    // uint faceType = uint( faceType_f );
    vec2 face_scale; // = vec2( 1, 1 );
    if ( faceType == FACE_TOP || faceType == FACE_BOTTOM ) {
        face_scale = mesh_size.xz;
    } else if ( faceType == FACE_RIGHT || faceType == FACE_LEFT ) {
        face_scale = mesh_size.zy;
    } else if ( faceType == FACE_FRONT || faceType == FACE_BACK ) {
        face_scale = mesh_size.xy;
    }
    float face_light;
    if ( faceType == FACE_TOP ) {
        face_light = 1.0;
    } else if ( faceType == FACE_BOTTOM ) {
        face_light = 0.5;
    } else {
        face_light = 0.75;
    }

    uint packed_lighting;
    if ( faceType == FACE_TOP || faceType == FACE_BOTTOM || faceType == FACE_RIGHT ) {
        packed_lighting = packed_lighting_1[ faceType ];
    } else { // For FACE_FRONT FACE_LEFT FACE_BACK
        packed_lighting = packed_lighting_2[ faceType - 3u ];
    }
    float light_divisor = 1.3; // good looking
    // light_divisor = 0.5;       // debug

    uint which_bits = 3u;
    if ( corner_shift == CORNER_OFFSET_c ) {
        which_bits = 7u;
    }
    float corner_light = float( ( packed_lighting >> corner_shift ) & which_bits ) / light_divisor; // Has to be a float to be interped over the shader;
    if ( corner_shift == CORNER_OFFSET_c ) {
        corner_light /= 2.0;
    }
    if ( packed_lighting == NO_LIGHT_NO_DRAW ) {
        v_shouldDiscardNoLight = 1;
    } else {
        v_shouldDiscardNoLight = 0;
    }
    corner_light = ( 3.9 - corner_light ) / 3.9;

    v_corner_lighting = face_light * corner_light;
    v_TexCoordBlock = texCoordBlock * face_scale;
    uint shift = ( faceType % 2u ) * 16u;
    // 0xffffu is max short 16u is sizeof(short)
    v_blockID = float( ( blockTexture[ faceType / 2u ] & ( 0xffffu << shift ) ) >> shift );
}
