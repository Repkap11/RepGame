#version 310 es

precision highp float;
precision lowp sampler2DArray;

in vec3 v_TexCoordBlock;
flat in ivec4 v_BlockCoords;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;
layout( location = 1 ) out ivec4 blockCoords;

void main( ) {
    vec4 texColor = texture( u_Texture, v_TexCoordBlock );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    color = texColor;

    blockCoords = v_BlockCoords;
    // blockCoords = ivec4( 1, 5, 10, 1 );
}
