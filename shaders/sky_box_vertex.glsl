#version 310 es

// See SkyVertex in sky_box.hpp
layout( location = 0 ) in vec3 position;
layout( location = 1 ) in vec2 texCoords;
out vec2 v_tex_coords;

void main( ) {
    gl_Position = vec4( position, 1 );
    v_tex_coords = texCoords;
}
