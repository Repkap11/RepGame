#version 310 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

in vec2 v_tex_coords;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( v_tex_coords, 0 ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    color = texColor;
}
