#version 310 es

precision highp float;
precision lowp sampler2DArray;

in vec2 v_TexCoordBlock;
in flat float blockID;
in flat float diffuse;
in flat float ambient;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 color;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( v_TexCoordBlock, blockID ) );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    color = texColor * vec4( vec3( diffuse, diffuse, diffuse ) + ambient, 1 );
}
