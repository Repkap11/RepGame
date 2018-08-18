#version 310 es

precision highp float;
precision lowp sampler2DArray;

// in vec2 v_TexCoordBlock;
// in flat float v_blockID;
// in float v_corner_lighting;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 f_color;

void main( ) {
    // vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, v_blockID ) );
    vec4 texColor = vec4( 1, 0, 1, 1 );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    // float corner_light = ( 3.0 - v_corner_lighting ) / 3.0;
    //_color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
    f_color = texColor;
}
