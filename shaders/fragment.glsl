#version 310 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
in flat uint blockID;
in float corner_lighting;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    float corner_light = ( 3.0 - corner_lighting ) / 3.0;
    color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
}
