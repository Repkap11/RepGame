#version 300 es
uniform mat4 u_MVP;

// From UIOverlayVertex
layout( location = 0 ) in vec2 screen_position;
layout( location = 1 ) in float is_block;
layout( location = 2 ) in vec3 texture_coord;
layout( location = 3 ) in vec4 tint;

flat out float v_is_block;
out vec3 v_TexCoordBlock;
out vec4 v_Tint;

void main( ) {
    gl_Position = u_MVP * vec4( screen_position, -0.1, 1 );
    v_TexCoordBlock = texture_coord;
    v_is_block = is_block;
    v_Tint = tint;
}
