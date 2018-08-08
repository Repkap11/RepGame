#version 310 es
uniform mat4 u_MVP;

// See CubeFace in block.h
// This defines all the triangles of a cube
layout( location = 0 ) in vec4 position;
layout( location = 1 ) in uvec2 texCoordBlock;
layout( location = 2 ) in uint faceType;

// See BlockCoords in chunk.h
// There is one of these per block
layout( location = 3 ) in vec3 blockCoords;
layout( location = 4 ) in vec3 blockTexture;

out vec3 v_TexCoordBlock;

void main( ) {
    gl_Position = u_MVP * ( position + vec4( blockCoords, 0 ) );
    // This face_adjusted isn't needed if you want a different texture per face (not the same on all sides)
    uint face_adjusted = faceType > uint( 2 ) ? uint( 2 ) : faceType;
    v_TexCoordBlock = vec3( texCoordBlock, blockTexture[ face_adjusted ] );
}
