#version 310 es
uniform mat4 u_MVP;

// See CubeFace in block.h
// This defines all the triangles of a cube
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoordBlock;
layout( location = 2 ) in uint faceType;

// See BlockCoords in chunk.h
// There is one of these per block
layout( location = 3 ) in vec3 blockCoords;
layout( location = 4 ) in vec3 mesh_size;
layout( location = 5 ) in vec3 blockTexture;

out vec2 v_TexCoordBlock;
out flat float blockID;

void main( ) {
    gl_Position = u_MVP * vec4( position * mesh_size + blockCoords, 1 );
    // This face_adjusted isn't needed if you want a different texture per face (not the same on all sides)
    uint face_adjusted = faceType > uint( 2 ) ? uint( 2 ) : faceType;
    vec2 face_scale = vec2( 1, 1 );
    if ( faceType == uint( 0 ) || faceType == uint( 1 ) ) {
        face_scale = mesh_size.xz;
    } else if ( faceType == uint( 2 ) || faceType == uint( 4 ) ) {
        face_scale = mesh_size.xy;
    } else if ( faceType == uint( 3 ) || faceType == uint( 5 ) ) {
        face_scale = mesh_size.zy;
    }
    v_TexCoordBlock = texCoordBlock * face_scale;
    blockID = blockTexture[ face_adjusted ];
}
