#version 300 es

precision highp float;

layout( location = 0 ) out vec4 color;

uniform float u_ExtraAlpha;
uniform int u_Blur;

in vec2 TexCoords;

uniform sampler2DMS u_Texture;
uniform usampler2DMS u_Stencil;
// uniform sampler2DMS u_Stencil;
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

uint stecilSample( ivec2 coord ) {
    return texelFetch( u_Stencil, coord, 0 ).r;
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
    ivec2 multiCoords = tex_to_multisaple( TexCoords );

    uint stencil4 = stecilSample( multiCoords );

    if ( u_Blur != 0 || stencil4 != 0u ) {
        int blurSize = 2;
        uint stencil0 = stecilSample( multiCoords + ivec2( -1, -1 ) * blurSize );
        uint stencil1 = stecilSample( multiCoords + ivec2( -1, 0 ) * blurSize );
        uint stencil2 = stecilSample( multiCoords + ivec2( -1, 1 ) * blurSize );
        uint stencil3 = stecilSample( multiCoords + ivec2( 0, -1 ) * blurSize );
        // uint stencil4 = stecilSample( multiCoords + ivec2( 0, 0 ) * blurSize );
        uint stencil5 = stecilSample( multiCoords + ivec2( 0, 1 ) * blurSize );
        uint stencil6 = stecilSample( multiCoords + ivec2( 1, -1 ) * blurSize );
        uint stencil7 = stecilSample( multiCoords + ivec2( 1, 0 ) * blurSize );
        uint stencil8 = stecilSample( multiCoords + ivec2( 1, 1 ) * blurSize );
        uint numValid = stencil0 + stencil1 + stencil2 + stencil3 + stencil4 + stencil5 + stencil6 + stencil7 + stencil8;

        vec4 color0 = textureSample( multiCoords + ivec2( -1, -1 ) * blurSize );
        vec4 color1 = textureSample( multiCoords + ivec2( -1, 0 ) * blurSize );
        vec4 color2 = textureSample( multiCoords + ivec2( -1, 1 ) * blurSize );
        vec4 color3 = textureSample( multiCoords + ivec2( 0, -1 ) * blurSize );
        vec4 color4 = textureSample( multiCoords + ivec2( 0, 0 ) * blurSize );
        vec4 color5 = textureSample( multiCoords + ivec2( 0, 1 ) * blurSize );
        vec4 color6 = textureSample( multiCoords + ivec2( 1, -1 ) * blurSize );
        vec4 color7 = textureSample( multiCoords + ivec2( 1, 0 ) * blurSize );
        vec4 color8 = textureSample( multiCoords + ivec2( 1, 1 ) * blurSize );
        finalColor = (                                //
                         float( stencil0 ) * color0 + //
                         float( stencil1 ) * color1 + //
                         float( stencil2 ) * color2 + //
                         float( stencil3 ) * color3 + //
                         float( stencil4 ) * color4 + //
                         float( stencil5 ) * color5 + //
                         float( stencil6 ) * color6 + //
                         float( stencil7 ) * color7 + //
                         float( stencil8 ) * color8 ) /
                     float( numValid );

    } else {
        finalColor = textureMultisample( multiCoords );
    }
    finalColor.a *= u_ExtraAlpha;
    color = finalColor;
}