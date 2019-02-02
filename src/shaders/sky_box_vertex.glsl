#version 300 es
uniform mat4 u_MVP;

// See SkyVertex in sky_box.hpp
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
out vec2 v_tex_coords;

void main( ) {
    gl_Position = u_MVP * vec4( position, 1 );
    v_tex_coords = texCoords;
}
