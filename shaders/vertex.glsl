#version 310 es
uniform mat4 u_MVP;

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

// out vec2 v_TexCoordBlock;
// out flat float v_blockID;
// out float v_corner_lighting;
out vec3 v_position;

void main( ) {
    v_position = position * mesh_size + blockCoords;
    // gl_Position = u_MVP * vec4( position * mesh_size + blockCoords, 1 );
    // uint faceType = uint( faceType_f );
    // // This face_adjusted isn't needed if you want a different texture per face (not the same on all sides)
    // uint face_adjusted = faceType > uint( 2 ) ? uint( 2 ) : faceType;
    // vec2 face_scale = vec2( 1, 1 );
    // if ( faceType == uint( 0 ) || faceType == uint( 1 ) ) {
    //     face_scale = mesh_size.xz;
    // } else if ( faceType == uint( 2 ) || faceType == uint( 4 ) ) {
    //     face_scale = mesh_size.zy;
    // } else if ( faceType == uint( 3 ) || faceType == uint( 5 ) ) {
    //     face_scale = mesh_size.xy;
    // }

    // float packed_lighting = 0.0;
    // if ( faceType == uint( 0 ) || faceType == uint( 1 ) || faceType == uint( 2 ) ) {
    //     packed_lighting = packed_lighting_1[ faceType ];
    // } else {
    //     packed_lighting = packed_lighting_2[ faceType - uint( 3 ) ];
    // }
    // // float light_divisor = 1.7;//good looking
    // float light_divisor = 0.1;                                                                                  // debug
    // corner_lighting = float( ( uint( packed_lighting ) >> uint( corner_shift ) ) & uint( 3 ) ) / light_divisor; // Has to be a float to be interped over the shader
    // v_TexCoordBlock = texCoordBlock * face_scale;
    // blockID = blockTexture[ face_adjusted ];
}
