#version 300 es
uniform mat4 u_MVP;

// From UIOverlayVertex
layout( location = 0 ) in vec2 screen_position;
layout( location = 1 ) in float is_block;
layout( location = 2 ) in vec4 texture_coord;

flat out float v_is_block;
out vec2 v_TexCoordBlock;
flat out float v_blockID;
out float v_alpha;

void main( ) {
    gl_Position = u_MVP * vec4( screen_position, -0.1, 1 );
    v_TexCoordBlock = texture_coord.xy;
    v_blockID = texture_coord.z;
    v_alpha = texture_coord.a;
    v_is_block = is_block;
}
