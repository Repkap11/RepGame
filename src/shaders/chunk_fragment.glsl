#version 300 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
in vec2 v_TexCoordBlock_orig;
flat in float v_blockID;
in float v_corner_lighting;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, v_blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    vec2 border_vec = abs( ( v_TexCoordBlock_orig - vec2( 0.5, 0.5 ) ) * 2.0 );
    float border = min( ( 1.0 - ( max( border_vec.x, border_vec.y ) ) ) * 20.0, 1.0 );

    float corner_light = ( ( 3.0 - v_corner_lighting ) / 3.0 ); // * border;
    color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
}
