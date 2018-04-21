#version 440 core
in vec3 v_TexCoordBlock;

uniform sampler2D u_Texture;
uniform sampler2DArray u_Texture_new;
uniform uint u_WhichTexture;

layout( location = 0 ) out vec4 color;

void main( ) {
    // vec4 texColor = texture( u_Texture, v_TexCoord );
    vec4 texColor = texture( u_Texture_new, v_TexCoordBlock );
    color = texColor;
    // color = vec4( 1, 1, 1, 1 );
}
