#version 310 es
uniform mat4 u_MVP;

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in uvec2 texCoordBlock;
layout( location = 2 ) in uint faceType;
layout( location = 3 ) in vec3 blockCoords;
layout( location = 4 ) in vec3 blockTexture;

out vec3 v_TexCoordBlock;
flat out ivec4 v_BlockCoords;

void main( ) {
    gl_Position = u_MVP * ( position + vec4( blockCoords, 0 ) );
    uint face_adjusted = faceType > uint( 2 ) ? uint( 2 ) : faceType;
    v_TexCoordBlock = vec3( texCoordBlock, blockTexture[ face_adjusted ] );
    v_BlockCoords = ivec4( blockCoords.xyz, faceType + uint( 1 ) );
}
