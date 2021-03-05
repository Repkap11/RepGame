#version 300 es

precision highp float;
precision lowp sampler2DArray;

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D u_Texture;

void main( ) {
    FragColor = texture( u_Texture, TexCoords );
    // FragColor.a = 0.5;
    // FragColor.r += TexCoords.x;
}