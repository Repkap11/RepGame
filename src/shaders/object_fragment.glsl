#version 300 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

in vec2 v_tex_coords;
in float v_light;
flat in float v_blockID;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( v_tex_coords, v_blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    texColor = texColor * v_light;
    color = texColor;

    // vec3 new_color = texColor.rgb / ( texColor.rgb + vec3( 1.0 ) );
    // new_color = pow( new_color, vec3( 1.0 / 2.2 ) );
    // color = vec4( new_color, texColor.a );
}
