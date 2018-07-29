#version 310 es
uniform mat4 u_MVP;

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in uvec2 texCoordBlock;
layout( location = 2 ) in uint faceType;
layout( location = 3 ) in vec3 blockCoords;
layout( location = 4 ) in vec3 blockTexture;

out vec3 v_TexCoordBlock;
out vec3 v_BlockCoords;

void main( ) {
    gl_Position = u_MVP * ( position + vec4( blockCoords, 0 ) );
    v_TexCoordBlock = vec3( texCoordBlock, blockTexture[ faceType ] );
    v_BlockCoords = blockCoords.xyz;
}
