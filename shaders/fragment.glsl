#version 320 es

precision highp float;
precision lowp sampler2DArray;

in vec2 te_TexCoordBlock;
in float te_blockID;
in float te_corner_light;
in vec3 te_PatchDistance;

uniform sampler2DArray u_Texture;

layout( location = 0 ) out vec4 f_color;

void main( ) {
    vec4 texColor = texture( u_Texture, vec3( te_TexCoordBlock, te_blockID ) );
    // vec4 texColor = vec4( 1, 0, 1, 1 );
    if ( texColor.a == 0.0 ) {
        discard;
    }
    //float border_lines = min( ( min( min( te_PatchDistance.x, te_PatchDistance.y ), te_PatchDistance.z ) ) * 20.0, 1.0 );
    float border_lines = 1.0;
    // border_lines = te_extra;
    float corner_light = border_lines * ( 3.0 - te_corner_light ) / 3.0;
    f_color = texColor * vec4( corner_light, corner_light, corner_light, 1 );
}
