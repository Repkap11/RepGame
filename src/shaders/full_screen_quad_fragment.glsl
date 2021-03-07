#version 300 es

precision highp float;
precision lowp sampler2DArray;

layout( location = 0 ) out vec4 color;

uniform float u_ExtraAlpha;
uniform float u_Blur;

in vec2 TexCoords;

uniform sampler2D u_Texture;

void main( ) {
    vec4 color0 = texture( u_Texture, TexCoords + vec2( -1.0, -1.0 ) * u_Blur );
    vec4 color1 = texture( u_Texture, TexCoords + vec2( -1.0, 0.0 ) * u_Blur );
    vec4 color2 = texture( u_Texture, TexCoords + vec2( -1.0, 1.0 ) * u_Blur );
    vec4 color3 = texture( u_Texture, TexCoords + vec2( 0.0, -1.0 ) * u_Blur );
    vec4 color4 = texture( u_Texture, TexCoords + vec2( 0.0, 0.0 ) * u_Blur );
    vec4 color5 = texture( u_Texture, TexCoords + vec2( 0.0, 1.0 ) * u_Blur );
    vec4 color6 = texture( u_Texture, TexCoords + vec2( 1.0, -1.0 ) * u_Blur );
    vec4 color7 = texture( u_Texture, TexCoords + vec2( 1.0, 0.0 ) * u_Blur );
    vec4 color8 = texture( u_Texture, TexCoords + vec2( 1.0, 1.0 ) * u_Blur );

    vec4 finalColor = ( color0 + color1 + color2 + color3 + color4 + color5 + color6 + color7 + color8 ) / 9.0f;
    // vec4 finalColor = color2;
    finalColor.a *= u_ExtraAlpha;

    color = finalColor;
}