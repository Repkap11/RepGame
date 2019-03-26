#version 300 es
uniform mat4 u_MVP;
uniform vec3 u_DebugScaleOffset;

// See CubeFace in block.h
// This defines all the triangles of a cube
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoordBlock;
layout( location = 2 ) in float faceType_f;
layout( location = 3 ) in float corner_shift;

// See BlockCoords in chunk.h
// There is one of these per block
layout( location = 4 ) in vec3 blockCoords;
layout( location = 5 ) in vec3 mesh_size;
layout( location = 6 ) in vec3 blockTexture;
layout( location = 7 ) in vec3 packed_lighting_1;
layout( location = 8 ) in vec3 packed_lighting_2;

out vec2 v_TexCoordBlock;
flat out float v_blockID;
flat out int v_shouldDiscardNoLight;
out float v_corner_lighting;

void main( ) {

    // vec3 adjust = vec3( 1, 1, 1 ) - position;
    // adjust.y = position.y;
    gl_Position = u_MVP * vec4( position * ( mesh_size - u_DebugScaleOffset ) + blockCoords, 1 );
    uint faceType = uint( faceType_f );
    // This face_adjusted isn't needed if you want a different texture per face (not the same on all sides)
    uint face_adjusted = faceType > uint( 2 ) ? uint( 2 ) : faceType;
    vec2 face_scale = vec2( 1, 1 );
    if ( faceType == uint( 0 ) || faceType == uint( 1 ) ) {
        face_scale = mesh_size.xz;
    } else if ( faceType == uint( 2 ) || faceType == uint( 4 ) ) {
        face_scale = mesh_size.zy;
    } else if ( faceType == uint( 3 ) || faceType == uint( 5 ) ) {
        face_scale = mesh_size.xy;
    }
    float face_light;
    if ( faceType == uint( 0 ) ) {
        face_light = 1.0;
    } else if ( faceType == uint( 1 ) ) {
        face_light = 0.5;
    } else {
        face_light = 0.75;
    }

    float packed_lighting = 0.0;
    if ( faceType == uint( 0 ) || faceType == uint( 1 ) || faceType == uint( 2 ) ) {
        packed_lighting = packed_lighting_1[ faceType ];
    } else {
        packed_lighting = packed_lighting_2[ faceType - uint( 3 ) ];
    }
    float light_divisor = 1.3; // good looking
    // light_divisor = 1.0;       // debug

    uint which_bits = uint( 3 );
    if ( corner_shift == 16.0 ) {
        which_bits = uint( 7 );
    }
    float corner_light = float( ( uint( packed_lighting ) >> uint( corner_shift ) ) & which_bits ) / light_divisor; // Has to be a float to be interped over the shader;
    if ( corner_shift == 16.0 ) {
        corner_light /= 2.0;
    }
    if ( packed_lighting == float( 0xfffff ) ) {
        v_shouldDiscardNoLight = 1;
    } else {
        v_shouldDiscardNoLight = 0;
    }
    corner_light = ( 3.9 - corner_light ) / 3.9;

    v_corner_lighting = face_light * corner_light;
    v_TexCoordBlock = texCoordBlock * face_scale;
    v_blockID = blockTexture[ face_adjusted ];
}
