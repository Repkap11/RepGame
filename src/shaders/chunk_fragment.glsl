#version 300 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
flat in float v_blockID;
in float v_corner_lighting;
flat in int v_shouldDiscardNoLight;
in float v_planarDot;
uniform float u_shouldDiscardAlpha;
uniform sampler2DArray u_Texture;
uniform float u_ReflectionHeight;

layout( location = 0 ) out vec4 color;

void main( ) {
    if ( v_shouldDiscardNoLight == 1 ) {
        discard;
    }
    if ( v_planarDot < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
    vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, v_blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    if ( u_shouldDiscardAlpha == 1.0f && texColor.a < 0.7 ) {
        discard;
    }

    float corner_light = v_corner_lighting;
    color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
}
