#version 440 core
in vec3 v_TexCoordBlock;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

void main( ) {
    vec4 texColor = texture( u_Texture, v_TexCoordBlock );
    if (texColor.a == 0.0){
        discard;
    }
    color = texColor;
    // color = vec4( 1, 1, 1, 1 );
}
