#version 310 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( 0, 0, 0 ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    color = vec4( 1, 0, 0, 1 );
}
