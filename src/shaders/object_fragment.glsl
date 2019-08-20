#version 300 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;
uniform float u_ExtraAlpha;
uniform float u_ReflectionHeight;

layout( location = 0 ) out vec4 color;

in vec2 v_tex_coords;
in float v_light;
flat in float v_blockID;
in float v_planarDot;

void main( ) {
    if ( v_planarDot < 0.0 && u_ReflectionHeight != 0.0 ) {
        discard;
    }
    vec4 texColor = texture( u_Texture, vec3( v_tex_coords, v_blockID ) );
    // vec4 texColor = vec4( 1, 0, 0, 1 );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    color = texColor * vec4( v_light, v_light, v_light, u_ExtraAlpha );
}
