#version 300 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
flat in float v_blockID;
in float v_corner_lighting;
flat in int v_shouldDiscardNoLight;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

void main( ) {
    if ( v_shouldDiscardNoLight == 1 ) {
        discard;
    }
    vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, v_blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }

    float corner_light = v_corner_lighting;
    color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
}
