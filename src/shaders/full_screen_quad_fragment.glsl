#version 300 es

precision highp float;
precision lowp sampler2DArray;

layout( location = 0 ) out vec4 color;

in vec2 TexCoords;

uniform sampler2D u_Texture;

void main( ) {
    vec4 finalColor = texture( u_Texture, TexCoords );
    // finalColor.g = TexCoords.x;

    color = finalColor;
}