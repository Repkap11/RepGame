#version 300 es

precision highp float;
precision lowp sampler2DArray;

uniform sampler2DArray u_Texture;

flat in float v_is_block;
in vec2 v_TexCoordBlock;
flat in float v_blockID;
in float v_alpha;

layout( location = 0 ) out vec4 color;

void main( ) {
    if ( v_is_block != 0.0 ) {
        color = texture( u_Texture, vec3( v_TexCoordBlock, v_blockID ) );
        color *= vec4( 1, 1, 1, v_alpha );
    } else {
        color = vec4( v_TexCoordBlock, v_blockID, v_alpha );
    }
}
