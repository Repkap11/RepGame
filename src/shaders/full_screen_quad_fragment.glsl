#version 300 es

precision highp float;

layout( location = 0 ) out vec4 color;

uniform float u_ExtraAlpha;
uniform int u_Blur;

in vec2 TexCoords;

uniform sampler2DMS u_Texture;
uniform int u_TextureSamples;

vec4 textureMultisample( ivec2 coord ) {
    vec4 colorMS = vec4( 0.0 );
    for ( int i = 0; i < u_TextureSamples; i++ )
        colorMS += texelFetch( u_Texture, coord, i );
    colorMS /= float( u_TextureSamples );
    return colorMS;
}

vec4 textureSample( ivec2 coord ) {
    return texelFetch( u_Texture, coord, 0 );
}

ivec2 tex_to_multisaple( vec2 texCoord ) {
    ivec2 vpCoords = textureSize( u_Texture );

    ivec2 texCoordMS;
    texCoordMS.x = int( float( vpCoords.x ) * texCoord.x );
    texCoordMS.y = int( float( vpCoords.y ) * texCoord.y );
    return texCoordMS;
}

void main( ) {
    vec4 finalColor;
    if ( u_Blur != 0 ) {
        ivec2 multiCoords = tex_to_multisaple( TexCoords );
        int blurSize = 3;
        vec4 color0 = textureSample( multiCoords + ivec2( -1, -1 ) * blurSize );
        vec4 color1 = textureSample( multiCoords + ivec2( -1, 0 ) * blurSize );
        vec4 color2 = textureSample( multiCoords + ivec2( -1, 1 ) * blurSize );
        vec4 color3 = textureSample( multiCoords + ivec2( 0, -1 ) * blurSize );
        vec4 color4 = textureSample( multiCoords + ivec2( 0, 0 ) * blurSize );
        vec4 color5 = textureSample( multiCoords + ivec2( 0, 1 ) * blurSize );
        vec4 color6 = textureSample( multiCoords + ivec2( 1, -1 ) * blurSize );
        vec4 color7 = textureSample( multiCoords + ivec2( 1, 0 ) * blurSize );
        vec4 color8 = textureSample( multiCoords + ivec2( 1, 1 ) * blurSize );
        finalColor = ( color0 + color1 + color2 + color3 + color4 + color5 + color6 + color7 + color8 ) / 9.0f;

    } else {
        finalColor = textureMultisample( tex_to_multisaple( TexCoords ) );
    }
    finalColor.a *= u_ExtraAlpha;
    color = finalColor;
}